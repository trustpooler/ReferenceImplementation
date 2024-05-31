//
//  main.cpp
//  TrustPoolerReferenceImplementation
//
//  Created by The Trust Pooler Authors on 30/5/2024.
//
//  Bare minimal implementation of 1) Mutex Pool and 2) Long Short Pool
//  This is NOT production code - purpose is to illustrate how the pools work
//  No error handling
//  No blockchain integration
//
//  https://trustpooler.xyz
//
// Tested on GCC 14.1 and Clang 18.1.0
//

#include <iostream>
#include <string>
#include <cmath>
#include <map>
#include <set>
#include <cassert>
#include "third_party/cxx-prettyprint/prettyprint.hpp"

// Trust Pooler namespace
namespace tp
{
    // We need to balance within 1 cent for this exercise, can refactor to handle Wei as required
    // Using doubles for this exercise, in production better to use uint64_t
    inline
    bool Close(double a, double b )
    {
        return fabs(a-b) < 0.01;
    }

    enum class Side { Long, Short, Neither };

    // Just add void print(std::ostream& os ) const {} to an object and we can stream it
    // This works for any object - must be in the tp namespace
    template<typename T>
    inline
    auto operator<<(std::ostream& os, const T& t) -> decltype( t.print(os), os )
    {
        t.print(os);
        return os;
    }

    // Base class for a transaction - no constructor for this exercise
    struct DefaultTX
    {
        using TxId = int;           // Transaction id
        using Amount = double;      // Amount we are risking
        
        TxId        id{};           // Transaction id
        Amount      amount{};       // Amount of capital at risk
        std::string client_account; // From account
        std::string pool_account;   // Pool fee account
        Amount      payout{};       // How much are we paying out (absolute currency amount)?
        
        void print(std::ostream& os ) const
        {
            os << "Tx id : " << id << " Amount : " << amount << " Payout : " << payout <<std::endl;
        }
    };

    // Base class for a Pool Event either Mutex or Long Short - no constructor for this exercise
    struct PoolEvent
    {
        double pool_share{};        // What share of the pool do I have >
        double winnings_share{};    // What share of the winnings do I have ?
        double payoff{};            // What payoff / odds have we calculated ? eg $1.20
        
        void print(std::ostream& os ) const
        {
            os << "Payoff : " << payoff << " share of pool : " << pool_share*100. << " % " << " share of winnings : " << winnings_share*100. << " % "<< std::endl;
        }
    };

    // Mutually exclusive event
    template <typename TX>
    struct MutexEvent : PoolEvent
    {
        using Tx = TX;
        using TxId = TX::TxId;
        using Amount = TX::Amount;
        using Level = std::string;
        
        std::string     event;      // String identifier of event, for stronger typing use an enum
        Tx              tx;         // Tx associated with this event
        
        MutexEvent()=default;
        MutexEvent( const std::string& e ) : event(e) {};
        
        void print( std::ostream& os ) const
        {
            os << "Event : " << event << " " << tx;
            PoolEvent::print(os);
        }
        
        // Is this a winning event ?
        constexpr
        bool IsWinner( Level level ) const noexcept
        {
            return event == level;
        }
        
        // If we have won - what is the raw amount ?
        constexpr
        Amount WinningAmount( Level level ) const noexcept
        {
            if ( level == event )   return tx.amount;
            return 0.;
        }
        
        // What category do we belong to ?
        constexpr
        std::string Category() const noexcept
        {
            return event;
        }
        
        constexpr
        Level GetLevel() const noexcept
        {
            return event;
        }
    };

    // Long Short Pool event
    template <typename TX>
    struct LongShortEvent : PoolEvent
    {
        using Tx = TX;
        using TxId = TX::TxId;
        using Amount = TX::Amount;
        using Price = int;
        using Level = int;
        
        Side        side{};     // Long or Short ?
        Price       price{};    // What price level ?
        Tx          tx;         // Associated tx
        
        double prima_facie_payoff{};                // Payoff / Odds before reweighting
        double prima_facie_payout{};                // Absolute $ / ETH payout
        double inverse_distance_to_the_pin{};       // Inverse distance to the pin
        double inverse_distance_to_pin_normalised;  // After normalisation
        double adjusted_amount{};                   // Adjusted amount
        
        LongShortEvent()=default;
        LongShortEvent( Side s, Price p ) : side{s}, price{p} {};
        
        void print( std::ostream& os ) const
        {
            os << ( side == Side::Long ? "Long " : "Short ") << " @ price " << price << " " << tx;
            PoolEvent::print(os);
        }
        
        // Ignore the side in the closing event - just look at the closing price
        constexpr
        bool IsWinner( Level level ) const noexcept
        {
            if ( side == Side::Long )
            {
                if ( level > price )    return true;
            }
            if ( side == Side::Short )
            {
                if ( level < price )    return true;
            }
            return false;
        }
        
        // What is the raw amount
        constexpr
        Amount WinningAmount( Level level  ) const noexcept
        {
            if ( side == Side::Long )
            {
                if ( level > price )    return tx.amount;
            }
            if ( side == Side::Short )
            {
                if ( level < price )    return tx.amount;
            }
            return 0.;
        }
        
        // We need this to reweight the winners pool
        constexpr
        double WinningInverseDistance( Price closing_price ) const noexcept
        {
            if ( side == Side::Long )
            {
                if ( closing_price > price )    return 1./(double)(closing_price - price);
            }
            if ( side == Side::Short )
            {
                if ( closing_price < price )    return 1./(double)(price - closing_price);
            }
            return 1.;
        }
        
        constexpr
        std::string Category() const noexcept
        {
            if ( side == Side::Long )    return "Long";
            if ( side == Side::Short )   return "Short";
            return "Error";
        }
        
        constexpr
        Level GetLevel() const noexcept
        {
            return price;
        }
    };

    // Generic interface for both Mutex and LongShort Pools
    struct PoolInterface
    {
        virtual std::string PoolManagerAccount() const = 0;                     // Account name, crypto address
        virtual std::string PoolAccount() const = 0;                            // Ditto
        virtual std::map< std::string, double > CategoryMap() const = 0;
    };

    // Use CRTP - static polymorphism
    // https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
    // D = Derived Implementation
    template <typename D, typename EVENT>
    struct Pool : PoolInterface
    {
        using Self      = Pool<D, EVENT>;
        using Risk      = EVENT;
        using Event     = EVENT;
        using Amount    = EVENT::Amount;
        using Level     = EVENT::Level;
        using Tx        = EVENT::Tx;
        using TxId      = EVENT::TxId;
        
        TxId                    tx{};           // TxId counter
        double                  fees{0.03};     // Pool fees - set to default 3%
        std::map< TxId, Risk >  risks;          // List of risks keyed on tx_id
        
        // Return the transaction id - this mutates the pool
        TxId MakeRisk( const Event& event, Amount amount, const std::string& who )
        {
            Event risk{ event };
            risk.tx.id = tx;
            risk.tx.amount = amount;
            risk.tx.client_account = who;
            risk.tx.pool_account = PoolAccount();
            risks[tx++]  = risk;
            return risk.tx.id;
        }
        
        // Note that this mutates the pool - we make a copy for the const version
        // Level is the outcome that we want to know about
        auto ProFormaReturnHelper( const Event& event, Amount amount, Level level )
        {
            // Put the hypothetical risk into pool
            auto tx_id = MakeRisk( event, amount, "Hypothetical" );
            auto winning_risks = static_cast<D*>(this)->MakeWinningRisks( level );
            try {
                return winning_risks.at( tx_id );            // We won
            } catch (...) {
                return Risk{};                              // Its a bust
            }
        }
        
        auto ProFormaReturn( const Event& event, Amount amount, Level level ) const
        {
            // Copy the pool
            Self pool{*this};
            return pool.ProFormaReturnHelper( event, amount, level );
        }
        
        // Set of unique end points
        std::set<Level> MakeLevelSet() const
        {
            std::set<Level> levels;
          
            for (const auto& [tx,risk] : risks )    levels.insert( risk.GetLevel() );
        
            // If dealing with numbers add one tick under/over
            if constexpr ( std::is_arithmetic_v<Level> )
            {
                auto min = *levels.begin();
                auto max = *levels.rbegin();
                
                levels.insert(min-1);   // One tick under
                levels.insert(max+1);   // One tick over
            }
            
            return levels;
        }
        
        template <typename CALLABLE, typename ... ARGS >
        void ForEachLevel( CALLABLE&& f, ARGS&&... args ) const
        {
            auto levels = MakeLevelSet();
            for (auto l : levels ) f( l, std::forward<ARGS>(args)... );
        }
        
        Amount TotalPool() const
        {
            Amount result{};
            for (auto& [tx,risk] : risks )    result += risk.tx.amount;
            return result;
        }
        
        // What amounts have won at a given closing price/event ?
        Amount TotalWinningAmount( Level level ) const
        {
            Amount  result{};
            for (auto& [tx,risk] : risks )    result += risk.WinningAmount( level );
            return result;
        }
        
        // What is the total winning amount for each price level - note that we mutuate here to get the unique price set
        Amount PoolWinningAmount() const
        {
            Amount result{};
            ForEachLevel( [&]( auto level ){ result += TotalWinningAmount( level ); } );
            return result;
        }
        
        std::size_t CountWinningRisks( Level level ) const
        {
            std::size_t n{0};
            for (auto& [tx,risk] : risks )    if ( risk.IsWinning( level ) )   ++n;
            return n;
        }
        
        Amount Fees() const
        {
            return TotalPool()*fees;
        }
        
        virtual std::string PoolManagerAccount() const override 
        {
            return "Pool_Manager_Address";
        }
        
        virtual std::string PoolAccount() const override
        {
            return "Pool_Account_Address";
        }
        
        virtual std::map< std::string, double > CategoryMap() const override
        {
            std::map< std::string, double > result;
            for (auto& [tx,risk] : risks )    result[ risk.Category() ] += risk.tx.amount;
            return result;
        }
          
        std::map< Level, double > ProFormaPayoffCurve( const Event& event, Amount amount)
        {
            std::map< Level, double > result;
            ForEachLevel(  [&]( auto level ){
                auto b = ProFormaReturn( event, amount, level );
                result[ level ] = b.payoff;
            } ) ;
            return result;
        }
    };

    // Now the specific implementation of a Mutex pool
    struct MutexPool : Pool< MutexPool, MutexEvent<DefaultTX> >
    {
        using Super = Pool< MutexPool, MutexEvent<DefaultTX> >;
        
        std::map< TxId, Risk > MakeWinningRisks( Level level ) const
        {
            std::map< TxId, Risk > winning_risks;
            
            // Can do these steps in parallel
            Amount total_pool_value = TotalPool()*(1.-fees);
            Amount total_win_value = TotalWinningAmount( level );
       
            //Checks
            double total_payout{};
            
            // Iterate over all of the risks pick the winner - we don't mtate risks
            for (const auto& [tx,risk] : risks ) {
                if ( risk.IsWinner( level ) )
                {
                    Risk winning_risk{risk};
                    
                    winning_risk.pool_share = risk.tx.amount / total_pool_value;
                    winning_risk.winnings_share = risk.tx.amount / total_win_value;
                    winning_risk.payoff = ( total_pool_value / total_win_value );
                    winning_risk.tx.payout = risk.tx.amount * winning_risk.payoff;
                    winning_risks[tx]=winning_risk;
                    
                    // Checks
                    total_payout += winning_risk.tx.payout;
                }
            }
            
            assert( Close( total_payout + Fees() , TotalPool() ) );
           
            std::cout << winning_risks << std::endl;
            std::cout << "Fees : " << Fees() << std::endl;
            std::cout << "Pool value : " << TotalPool() << std::endl;
            std::cout << "Total payout : " << total_payout << std::endl;
     
            return winning_risks;
        }
    };

    // Now the specific implementation of a LongShort pool
    struct LongShortPool : Pool< LongShortPool, LongShortEvent<DefaultTX> >
    {
        using Super = Pool< LongShortPool, LongShortEvent<DefaultTX> >;
        
        std::map< TxId, Risk > MakeWinningRisks( Level level ) const
        {
            std::map< TxId, Risk > winning_risks; //.clear();
            
            // Can do these steps in parallel
            Amount total_pool_value = TotalPool()*(1.-fees);
            Amount total_win_value = TotalWinningAmount( level );
       
            double total_inverse_distance_to_pin{};
            
            //Checks
            double total_prima_facie_payout{}, total_payout{};
            
            // Iterate over all of the risks pick the winner - we don't mutate
            for (const auto& [tx,risk] : risks ) {
                if ( risk.IsWinner( level ) )
                {
                    Risk winning_risk{risk};
                    
                    winning_risk.pool_share = risk.tx.amount / total_pool_value;
                    winning_risk.winnings_share = risk.tx.amount / total_win_value;
                    winning_risk.prima_facie_payoff = ( total_pool_value / total_win_value );
                    winning_risk.prima_facie_payout = risk.tx.amount * winning_risk.prima_facie_payoff;
                    
                    // Adjust the amount in proportion to the distance to the pin
                    winning_risk.inverse_distance_to_the_pin = risk.WinningInverseDistance( level );
                    total_inverse_distance_to_pin += winning_risk.inverse_distance_to_the_pin;
                    
                    winning_risks[tx]=winning_risk;
                    
                    // Checks
                    total_prima_facie_payout += winning_risk.prima_facie_payout;
                }
            }
            
            // Now iterate over the winners - we mutate the winners here
            for ( auto& [tx, winning_risk] : winning_risks ) {
                winning_risk.inverse_distance_to_pin_normalised = winning_risk.inverse_distance_to_the_pin / total_inverse_distance_to_pin;
                winning_risk.adjusted_amount = winning_risk.inverse_distance_to_pin_normalised * total_win_value;     // Redistribute the winning pool based on the inverse distance
                winning_risk.tx.payout = winning_risk.adjusted_amount * winning_risk.prima_facie_payoff;
                winning_risk.payoff = winning_risk.tx.payout / winning_risk.tx.amount;
                
                // Check
                total_payout += winning_risk.tx.payout;
            }
            
            assert( Close( total_prima_facie_payout + Fees() , TotalPool() ) );
            assert( Close( total_prima_facie_payout, total_payout ) );
            
            std::cout << "Closing price : " << level << std::endl;
            std::cout << winning_risks << std::endl;
            std::cout << "Total prima facie payout : " << total_prima_facie_payout << std::endl;
            std::cout << "Fees : " << Fees() << std::endl;
            std::cout << "Pool value : " << TotalPool() << std::endl;
            std::cout << "Total payout : " << total_payout << std::endl;
            
            return winning_risks;
        }
    };
};

int main(int argc, const char * argv[]) {
    
    using namespace tp;
    
    MutexPool mutex_pool;
    mutex_pool.MakeRisk( MutexPool::Event{"default"},    500,    "barney" );
    mutex_pool.MakeRisk( MutexPool::Event{"default"},    2500,   "barney" );
    mutex_pool.MakeRisk( MutexPool::Event{"no_default"}, 10000,  "arnold"    );
    mutex_pool.MakeRisk( MutexPool::Event{"no_default"}, 5000,   "arnold"    );
    
    auto mutex_levels = mutex_pool.MakeLevelSet();
    auto mutex_total_pool = mutex_pool.TotalPool();
    auto mutex_total_winnning_amount = mutex_pool.TotalWinningAmount( "default" );
    
    std::cout << mutex_pool.CategoryMap() << std::endl;
    mutex_pool.MakeWinningRisks("default");
    auto mutex_pro_forma = mutex_pool.ProFormaReturnHelper( MutexPool::Event{"default"}, 1000, "default" );
    
    LongShortPool ls_pool;
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Long, 50}, 500, "barney" );
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Long, 55}, 250, "barney");
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Long, 60}, 1000, "barney");
    
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Short, 60}, 700, "arnold");
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Short, 55}, 900, "arnold");
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Short, 50}, 1000, "arnold");
    ls_pool.MakeRisk( LongShortPool::Event{ Side::Short, 40}, 1500, "arnold");
    
    auto ls_levels = ls_pool.MakeLevelSet();
    auto ls_total_pool = ls_pool.TotalPool();
    auto ls_total_winnning_amount = ls_pool.TotalWinningAmount( 56 );
    
    std::cout << ls_pool.CategoryMap() << std::endl;
    ls_pool.MakeWinningRisks(56);
    
    auto ls_curve = ls_pool.ProFormaPayoffCurve( LongShortPool::Event{ Side::Long,  50}, 500 );
    
    // Don't mutate the pool
    auto ls_pro_forma_long  = ls_pool.ProFormaReturn( LongShortPool::Event{ Side::Long,  50}, 1000, 51 );
    auto ls_pro_forma_short = ls_pool.ProFormaReturn( LongShortPool::Event{ Side::Short, 50}, 1000, 49 );
    
    auto ls_pro_forma_long_check  = ls_pool.ProFormaReturnHelper( LongShortPool::Event{ Side::Long,  50}, 1000, 51 );
    auto ls_pro_forma_short_check = ls_pool.ProFormaReturnHelper( LongShortPool::Event{ Side::Short, 50}, 1000, 49 );
    
    return 0;
}
