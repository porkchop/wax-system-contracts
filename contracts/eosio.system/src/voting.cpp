#include <eosio/crypto.hpp>
#include <eosio/datastream.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/privileged.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>

#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
   uint64_t to_int(const eosio::checksum256& value) {
      auto byte_array = value.extract_as_byte_array();

      uint64_t int_value = 0;
      for (int i = 0; i < 8; i++) {
         int_value <<= 8;
         int_value |= byte_array[i] & 127;
      }
      return int_value;
   }

} // namespace

namespace eosiosystem {

   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::singleton;

   void system_contract::regproducer( const name& producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      check( url.size() < 512, "url too long" );
      check( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );

      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();

      auto add_reward_info = [&]() {
         _rewards.emplace( producer, [&]( reward_info& info ){
            info.init(producer);

            // If we only have 21 producers or less they are ready to produce, otherwise
            // they will have to wait to be selected
            /// @todo It's necessary to check for "active" producers.
            if (std::distance(_producers.cbegin(), _producers.cend()) <= 21)
               info.set_current_type(reward_type::producer);
         });
      };

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
            info.producer_key = producer_key;
            info.is_active    = true;
            info.url          = url;
            info.location     = location;
            if ( info.last_claim_time == time_point() )
               info.last_claim_time = ct;
         });

         if (_greward.activated) {
            if (auto it = _rewards.find(producer.value); it == _rewards.end())
               add_reward_info();
         }


      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner           = producer;
            info.total_votes     = 0;
            info.producer_key    = producer_key;
            info.is_active       = true;
            info.url             = url;
            info.location        = location;
            info.last_claim_time = ct;
         });

         if (_greward.activated)
            add_reward_info();
      }

   }

   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }

   /**
    * Returns true when the percent (in the last 24 hours) of standby produced
    * blocks is less than 1% otherwise returns false
    */
   bool system_contract::is_it_time_to_select_a_standby() const {
      auto& stb_cnt = _greward.get_counters(reward_type::standby);
      auto& pro_cnt = _greward.get_counters(reward_type::producer);

      uint64_t stb_total = std::accumulate(
         stb_cnt.unpaid_blocks_per_hour.begin(), stb_cnt.unpaid_blocks_per_hour.end(), 0);

      uint64_t total = stb_total + std::accumulate(
         pro_cnt.unpaid_blocks_per_hour.begin(), pro_cnt.unpaid_blocks_per_hour.end(), 0);

      if (total > 0) {
         double percent = 100.0 * stb_total / total;
         return percent < 1.0;
      }

      return false;
   }

   /**
    * Updates the reward status of all producers (including those that are not top producers)
    */
   void system_contract::update_producer_reward_status(int64_t schedule_version) {
      auto it_ver = _greward.proposed_top_producers.find(schedule_version);

      if (it_ver == _greward.proposed_top_producers.end())
         // "status" by version was already applied, nothing to do
         return;

      for(const auto& old_top_prod: _greward.current_producers) {
         if (auto reward_it = _rewards.find(old_top_prod.first.value); reward_it != _rewards.end()) {
            _rewards.modify(reward_it, same_payer, [&](auto& rec) {
               rec.set_current_type(reward_type::none);
            });
         }
      }

      for(const auto& new_top_prod: it_ver->second) {
         if (auto reward_it = _rewards.find(new_top_prod.first.value); reward_it != _rewards.end()) {
            _rewards.modify(reward_it, same_payer, [&](auto& rec) {
               rec.current_type = new_top_prod.second; // raw uint32 type
            });
         }
      }

      _greward.current_producers = it_ver->second;

      do {
        // Top producer status applied, remove information
        _greward.proposed_top_producers.erase(it_ver);
        // In the odd case that we skip a version, erase previous adjacent versions...
      }
      while (--schedule_version >= 0 && (it_ver = _greward.proposed_top_producers.find(schedule_version)) != _greward.proposed_top_producers.end());
   }

   /**
    * Selects the specified producer range into the result vector. It also adds the
    * provided status to that vector. 
    */
   void system_contract::select_producers_into( uint64_t begin, 
                                                uint64_t count,
                                                reward_type type, 
                                                prod_vec_t& result ) {
      auto idx = _producers.get_index<"prototalvote"_n>();
      uint64_t i = 0;

      for (auto it = idx.cbegin(); 
           it != idx.cend() && i < (begin + count) && 0 < it->total_votes && it->active(); 
           ++it, ++i) 
      {
         if (i >= begin)
            result.emplace_back(
               prod_vec_t::value_type{{it->owner, it->producer_key}, it->location, type});
      }
   }

   void system_contract::update_elected_producers( const block_timestamp& block_time, 
                                                   const eosio::checksum256& previous_block_hash ) {
      _gstate.last_producer_schedule_update = block_time;

      prod_vec_t top_producers;
      top_producers.reserve(21);

      select_producers_into(0, 21, reward_type::producer, top_producers);

      if (top_producers.size() == 0 || top_producers.size() < _gstate.last_producer_schedule_size ) {
         eosio::print("No top producers or they are less than the last scheduled");
         return;
      }
      
      if (is_it_time_to_select_a_standby()) {
         prod_vec_t standbys; standbys.reserve(max_standbys);

         // Pick the current 36 standbys
         select_producers_into(21, max_standbys, reward_type::standby, standbys);

         uint64_t previous_block_hash_int = to_int(previous_block_hash);
         const uint64_t standby_index = previous_block_hash_int % max_standbys;

         if (standbys.size() > standby_index) {
            // Add the selected standby as an elected top producer.
            top_producers[previous_block_hash_int % 21] = standbys[standby_index];

            /// @todo The following print statement isn't working. Check it.
            //eosio::print_f("Selected standby producer: %\n",
            //   std::get<0>(standbys[standby_index]).producer_name.to_string());
         }
      }

      // sort by producer name, if both are equal it will sort by location
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;
      producers.reserve(top_producers.size());

      for( const auto& item : top_producers )
         producers.push_back(std::get<0>(item));

      // Proposes a new list
      if (auto version = set_proposed_producers(producers); version.has_value()) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );

         if (auto it = _greward.proposed_top_producers.find(*version); it != _greward.proposed_top_producers.end())
            return;

         top_prod_vec_t new_top_producers;
         new_top_producers.reserve(21);

         // Map 'top_producers' to 'new_top_producers'
         using namespace std;
         transform(
            top_producers.begin(),
            top_producers.end(),
            back_inserter(new_top_producers),
            [](const auto& prod_tuple) {
               return pair(get<0>(prod_tuple).producer_name, enum_cast(get<2>(prod_tuple)));
            });

         _greward.proposed_top_producers.emplace(*version, new_top_producers);
      }
   }

   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      double weight = int64_t( (current_time_point().sec_since_epoch() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 13 );
      return double(staked) * std::pow( 2, weight );
   }

   void system_contract::voteproducer( const name& voter_name, const name& proxy, const std::vector<name>& producers ) {
      require_auth( voter_name );
      update_votes( voter_name, proxy, producers, true );
   }

   void system_contract::update_votes( const name& voter_name, const name& proxy, const std::vector<name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         check( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         check( voter_name != proxy, "cannot proxy to self" );
      } else {
         check( producers.size() <= 30, "attempt to vote for too many producers" );
         for( size_t i = 1; i < producers.size(); ++i ) {
            check( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      auto voter = _voters.find( voter_name.value );
      check( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      check( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      /**
       * The first time someone votes we calculate and set last_vote_weight, since they cannot unstake until
       * after total_activated_stake hits threshold, we can use last_vote_weight to determine that this is
       * their first vote and should consider their stake activated.
       */
      if( voter->last_vote_weight <= 0.0 ) {
         _gstate.total_activated_stake += voter->staked;
         if( _gstate.total_activated_stake >= min_activated_stake && _gstate.thresh_activated_stake_time == time_point() ) {
            _gstate.thresh_activated_stake_time = current_time_point();
         }
      }

      auto new_vote_weight = stake2vote( voter->staked );
      if( voter->is_proxy ) {
         new_vote_weight += voter->proxied_vote_weight;
      }

      std::map<name, std::pair<double, bool /*new*/> > producer_deltas;
      if ( voter->last_vote_weight > 0 ) {
         if( voter->proxy ) {
            auto old_proxy = _voters.find( voter->proxy.value );
            check( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
            _voters.modify( old_proxy, same_payer, [&]( auto& vp ) {
                  vp.proxied_vote_weight -= voter->last_vote_weight;
               });
            propagate_weight_change( *old_proxy );
         } else {
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         auto new_proxy = _voters.find( proxy.value );
         check( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         check( !voting || new_proxy->is_proxy, "proxy not found" );
         if ( new_vote_weight >= 0 ) {
            _voters.modify( new_proxy, same_payer, [&]( auto& vp ) {
                  vp.proxied_vote_weight += new_vote_weight;
               });
            propagate_weight_change( *new_proxy );
         }
      } else {
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p];
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      for( const auto& pd : producer_deltas ) {
         auto pitr = _producers.find( pd.first.value );
         if( pitr != _producers.end() ) {
            if( voting && !pitr->active() && pd.second.second /* from new set */ ) {
               check( false, ( "producer " + pitr->owner.to_string() + " is not currently registered" ).data() );
            }
            double init_total_votes = pitr->total_votes;
            _producers.modify( pitr, same_payer, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
               //check( p.total_votes >= 0, "something bad happened" );
            });
         } else {
            if( pd.second.second ) {
               check( false, ( "producer " + pd.first.to_string() + " is not registered" ).data() );
            }
         }
      }

      _voters.modify( voter, same_payer, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.producers = producers;
         av.proxy     = proxy;
      });
      update_voter_votepay_share(voter);
   }

   void system_contract::regproxy( const name& proxy, bool isproxy ) {
      require_auth( proxy );

      auto pitr = _voters.find( proxy.value );
      if ( pitr != _voters.end() ) {
         check( isproxy != pitr->is_proxy, "action has no effect" );
         check( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, same_payer, [&]( auto& p ) {
               p.is_proxy = isproxy;
            });
         propagate_weight_change( *pitr );
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
               p.owner  = proxy;
               p.is_proxy = isproxy;
            });
      }
   }

   void system_contract::voterclaim(const name owner) {
      int64_t reward = collect_voter_reward(owner);

      eosio::token::transfer_action transfer_act{ token_account, { {voters_account, active_permission}, {owner, active_permission} } };
      transfer_act.send( voters_account, owner, asset(reward, core_symbol()), "voter pay" );
   }

   void system_contract::claimgbmvote(const name owner) {
      int64_t reward = collect_voter_reward(owner);

      send_genesis_token( voters_account, owner, asset(reward, core_symbol()));
   }

   int64_t system_contract::collect_voter_reward(const name owner) {
      require_auth(owner);

      check( _gstate.total_activated_stake >= min_activated_stake,
                    "cannot claim rewards until the chain is activated (at least 15% of all tokens participate in voting)" );

      const auto& voter = _voters.get(owner.value, "voter does not exist.");

      check(voter.unpaid_voteshare_last_updated != time_point(), "you need to vote first! unpaid_voteshare_last_updated is zero.");

      auto ct = current_time_point();

      check( ct - voter.last_claim_time > microseconds(useconds_per_day), "already claimed rewards within past day" );

      fill_buckets();
      _gstate.total_unpaid_voteshare += _gstate.total_voteshare_change_rate * double((ct - _gstate.total_unpaid_voteshare_last_updated).count() / 1E6);
      _gstate.total_unpaid_voteshare_last_updated = ct;
      check(_gstate.total_unpaid_voteshare > 0, "no rewards available.");

      double producers_performance = calculate_producers_performance(voter);
      double unpaid_voteshare = voter.unpaid_voteshare + producers_performance * voter.unpaid_voteshare_change_rate * double((ct - voter.unpaid_voteshare_last_updated).count() / 1E6);

      int64_t reward = _gstate.voters_bucket * (unpaid_voteshare / _gstate.total_unpaid_voteshare);
      check(reward > 0, "no rewards available.");

      if (reward > _gstate.voters_bucket) {
         reward = _gstate.voters_bucket;
      }

      _gstate.voters_bucket -= reward;
      _gstate.total_unpaid_voteshare -= unpaid_voteshare;
      _voters.modify(voter, same_payer, [&]( auto& v ) {
         v.unpaid_voteshare = 0;
         v.unpaid_voteshare_last_updated = ct;
         v.last_claim_time = ct;
      });

      return reward;
   }

   double system_contract::single_producer_performance( const reward_info& rewards ) const {
     auto current_type = rewards.get_current_type();
     auto first_check = current_type == reward_type::none ? reward_type::standby : current_type;
     auto second_check = first_check == reward_type::producer ? reward_type::standby : reward_type::producer;
     reward_type check_types[] {first_check, second_check};

     for(auto type : check_types) {
       auto counters = rewards.get_counters(type);
       if(_gstate2.last_block_num.slot - counters.previous_performance_start_block.slot > block_accuracy_sample_size * 3)
         continue;
       switch(type) {
         case reward_type::standby:
           return std::min(1., counters.previous_performance_blocks / (0.97 * block_accuracy_sample_size));
         case reward_type::producer:
           return std::min(1., counters.previous_performance_blocks / (0.99 * block_accuracy_sample_size));
       }
     }

     return -1;
   }

   double system_contract::calculate_producers_performance( const voter_info& voter ) {
     if (_greward.activated) {
       std::vector<double> producer_performances;
       const auto& voter_or_proxy = voter.proxy
         ? _voters.get( voter.proxy.value, "proxy not found" ) //data corruption
         : voter;

       for( const auto& producer : voter_or_proxy.producers ) {
         auto rewards = _rewards.get( producer.value, "producer not found" );
         double perf = single_producer_performance(rewards);
         if(perf == -1) {
           perf = _greward.average_producers_performance();
         }
         producer_performances.push_back(perf);
       }

       while(producer_performances.size() < num_performance_producers) {
         producer_performances.push_back(_greward.average_producers_performance());
       }

       std::sort(producer_performances.begin(), producer_performances.end(), [&](double a, double b) {
         return a > b;
       });
       if(producer_performances.size() > num_performance_producers) {
         producer_performances.erase(producer_performances.begin() + num_performance_producers, producer_performances.end());
       }

       double performance = accumulate(producer_performances.begin(), producer_performances.end(), 0) / num_performance_producers;
       _greward.update_performance(performance);
       return performance;
     }

     return 1;
   }

   void system_contract::update_voter_votepay_share(const voters_table::const_iterator& voter_itr) {
      auto ct = current_time_point();
      double new_unpaid_voteshare = voter_itr->unpaid_voteshare;
      if (voter_itr->unpaid_voteshare_last_updated != time_point() && voter_itr->unpaid_voteshare_last_updated < current_time_point()) {
         double producers_performance = calculate_producers_performance(*voter_itr);
         new_unpaid_voteshare += producers_performance * voter_itr->unpaid_voteshare_change_rate * double((ct - voter_itr->unpaid_voteshare_last_updated).count() / 1E6);
      }
      double new_change_rate{0};
      if(voter_itr->producers.size() >= 16 || voter_itr->proxy){
         new_change_rate = voter_itr->last_vote_weight - voter_itr->proxied_vote_weight;
      }
      double change_rate_delta = new_change_rate - voter_itr->unpaid_voteshare_change_rate;
      
      if (_gstate.total_unpaid_voteshare_last_updated != time_point() && _gstate.total_unpaid_voteshare_last_updated < current_time_point()) {
         _gstate.total_unpaid_voteshare += _gstate.total_voteshare_change_rate * double((ct - _gstate.total_unpaid_voteshare_last_updated).count() / 1E6);
      }

      eosio::print("Calculating _gstate.total_voteshare_change_rate: ", change_rate_delta);
      _gstate.total_voteshare_change_rate += change_rate_delta;
      _gstate.total_unpaid_voteshare_last_updated = ct;

      _voters.modify(voter_itr, same_payer, [&](auto& v) {
         v.unpaid_voteshare = new_unpaid_voteshare;
         v.unpaid_voteshare_last_updated = ct;
         v.unpaid_voteshare_change_rate = new_change_rate;
      });
   }

   void system_contract::propagate_weight_change( const voter_info& voter ) {
      check( !voter.proxy || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy" );
      double new_weight = stake2vote( voter.staked );
      if ( voter.is_proxy ) {
         new_weight += voter.proxied_vote_weight;
      }

      /// don't propagate small changes (1 ~= epsilon)
      if ( fabs( new_weight - voter.last_vote_weight ) > 1 )  {
         if ( voter.proxy ) {
            auto& proxy = _voters.get( voter.proxy.value, "proxy not found" ); //data corruption
            _voters.modify( proxy, same_payer, [&]( auto& p ) {
                  p.proxied_vote_weight += new_weight - voter.last_vote_weight;
               }
            );
            propagate_weight_change( proxy );
         } else {
            auto delta = new_weight - voter.last_vote_weight;
            for ( auto acnt : voter.producers ) {
               auto& prod = _producers.get( acnt.value, "producer not found" ); //data corruption
               const double init_total_votes = prod.total_votes;
               _producers.modify( prod, same_payer, [&]( auto& p ) {
                  p.total_votes += delta;
                  _gstate.total_producer_vote_weight += delta;
               });
            }
         }
      }
      _voters.modify( voter, same_payer, [&]( auto& v ) {
            v.last_vote_weight = new_weight;
         }
      );
   }

} /// namespace eosiosystem
