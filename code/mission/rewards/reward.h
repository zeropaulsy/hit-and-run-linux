#ifndef REWARD_H
#define REWARD_H

#include <p3d/p3dtypes.hpp>

//base class for rewards
class Reward
    {
    public:
        enum eRewardType
            {
				eNULL,
				ALT_SKIN_OTHER,
                ALT_SKIN_GOOD,
                ALT_PLAYERCAR,
                FE_TOY,              
                NUM_REWARDS
            };
			enum eQuestType
			{
				eBlank,
                eDefaultCar,
                eDefaultSkin,
				eCards,
				eStreetRace,
				eBonusMission,
                eGoldCards, // TC: Is this obsolete now??
				NUM_QUESTS
#ifdef RAD_LINUX
                ,
                // Linux port: Mod Launcher CustomRewardQuestSupport's quest types (past NUM_QUESTS: the game's
                // per-level slots stay as they were; these rewards are RewardsManager's mod rewards)
                eModFirst = NUM_QUESTS,
                eWasps = eModFirst,
                eGags,
                eMission,
                eWager,
                eCoins,
                eHitAndRun,
                eEvaded,
                eBusted,
                eGetIn,
                eDoorbell,
                eMaxCoins,
                NUM_MOD_QUESTS
#endif
			};

        Reward ();
        Reward (char* name, int level, Reward::eRewardType type, Reward::eQuestType qtype);
        virtual ~Reward ();
        void UnlockReward ();
        void LockReward ();
        bool RewardStatus ();
        tUID GetUID() const;
        void SetLevel( int level );
        int GetLevel() const;
        char* GetName ();
        void SetName(char* name );
        char* GetFile ();
        void SetFilename (char* file );
        void SetRewardType (Reward::eRewardType type);
        void SetQuestType(Reward::eQuestType type);
        eRewardType GetRewardType ( );
        eQuestType GetQuestType ( );
        void SetRepairCost( int cost );
        int GetRepairCost() const;
#ifdef RAD_LINUX
        // Linux port: CustomRewardQuestSupport's extra BindReward argument (card / race / mission number, time,
        // coins, count: number; getin / doorbell: name)
        void SetModQuestArg( const char* arg );
        bool HasModQuestArg() const { return mModQuestArg[ 0 ] != '\0'; }
        int GetModQuestNumber() const;
        const char* GetModQuestArg() const { return mModQuestArg; }
    private:
        char mModQuestArg[ 32 ];
#endif
    private:
        tUID mUID;
        int mLevel;
        char mName [16];
        char mFile[64];
        bool mEarned;
		eQuestType mQuestType;
        eRewardType mRewardType;
        int mRepairCost;
    };

inline tUID Reward::GetUID() const
{
    return mUID;
}

inline void Reward::SetLevel( int level )
{
    mLevel = level;
}

inline int Reward::GetLevel() const
{
    return mLevel;
}

inline bool Reward::RewardStatus ()
    {
        return mEarned;
    }

inline  Reward::eRewardType Reward::GetRewardType ()
    {
        return mRewardType;
    }
    
inline Reward::eQuestType Reward::GetQuestType ()
    {
        return mQuestType;
    }


inline char* Reward::GetName ()
    {
        return mName;

    }
/*
    inline void Reward::UnlockReward ()
    {
        mEarned = true;
    }

*/

inline void Reward::LockReward ()
    {
        mEarned = false;
    }
inline char* Reward::GetFile()
    {
        return mFile;
    }

inline void Reward::SetRepairCost( int cost )
{
    mRepairCost = cost;
}

inline int Reward::GetRepairCost() const
{
    return mRepairCost;
}

#endif // REWARD_H

