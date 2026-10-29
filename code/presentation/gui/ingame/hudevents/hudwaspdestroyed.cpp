//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   HudWaspDestroyed
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/02/05		TChu		Created
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/ingame/hudevents/hudwaspdestroyed.h>
#include <presentation/gui/utility/specialfx.h>

#include <mission/gameplaymanager.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <mission/rewards/rewardsmanager.h>
#ifdef RAD_LINUX
#include <mission/charactersheet/modstatstotals.h>
#include <mission/rewards/modrewardquests.h>
#include <memory/srrmemory.h>
#endif

// Scrooby
#include <Page.h>
#include <Group.h>
#include <Text.h>

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

//===========================================================================
// Public Member Functions
//===========================================================================

HudWaspDestroyed::HudWaspDestroyed( Scrooby::Page* pPage )
:   HudEventHandler( pPage->GetGroup( "WaspsDestroyed" ) ),
    m_currentSubState( 0 ),
    m_itemsComplete( NULL ),
    m_isGagInsteadOfWasp( false )
{
    m_itemsComplete = m_drawableGroup->GetText( "WaspsComplete" );
    rAssert( m_itemsComplete != NULL );
#ifdef RAD_LINUX
    m_isModMessage = false;
    m_modMessageIndex = -1;
    if( ModRewardQuests::Active() && m_itemsComplete != NULL )
    {
        m_modMessageIndex = m_itemsComplete->GetNumOfStrings();
        m_itemsComplete->AddHardCodedString( " " );
    }
#endif
}

HudWaspDestroyed::~HudWaspDestroyed()
{
}

void
HudWaspDestroyed::Start()
{
    RenderEnums::LevelEnum currentLevel = GetGameplayManager()->GetCurrentLevelIndex();

    bool isComplete = m_isGagInsteadOfWasp ?
                      GetCharacterSheetManager()->QueryNumGagsViewed( currentLevel ) == GetRewardsManager()->GetTotalGags( currentLevel ) :
                      GetCharacterSheetManager()->QueryNumWaspsDestroyed( currentLevel ) == GetRewardsManager()->GetTotalWasps( currentLevel );

#ifdef RAD_LINUX
    // Linux port: Mod Launcher CustomStatsTotals, "all found" against the mod's wasp / gag totals
    if( ModStatsTotals::Active() )
    {
        int total = m_isGagInsteadOfWasp ? ModStatsTotals::Gags( currentLevel ) : ModStatsTotals::Wasps( currentLevel );
        int found = m_isGagInsteadOfWasp ? GetCharacterSheetManager()->QueryNumGagsViewed( currentLevel )
                                         : GetCharacterSheetManager()->QueryNumWaspsDestroyed( currentLevel );
        isComplete = total > 0 && found == total;
    }
#endif

#ifdef RAD_LINUX
    if( isComplete && m_isModMessage && this->IsActive() )
    {
        return;     // a quest complete message takes precedence
    }
#endif

    if( isComplete )
    {
#ifdef RAD_LINUX
        m_isModMessage = false;
#endif
        this->OnStart();

        m_currentSubState = STATE_WASP_TRANSITION_IN;

        // restore drawable properties
        //
        rAssert( m_itemsComplete != NULL );
        m_itemsComplete->ResetTransformation();
        m_itemsComplete->SetAlpha( 1.0f );
        m_itemsComplete->SetIndex( m_isGagInsteadOfWasp ? 1 : 0 );
    }
}

#ifdef RAD_LINUX
void
HudWaspDestroyed::StartModMessage( UnicodeString& text )
{
    if( m_modMessageIndex < 0 )
    {
        return;
    }
    HeapMgr()->PushHeap( GMA_LEVEL_HUD );
    m_itemsComplete->SetString( m_modMessageIndex, text );
    HeapMgr()->PopHeap( GMA_LEVEL_HUD );

    this->OnStart();
    m_elapsedTime = 0.0f;   // replacing a running wasp / gag message
    m_isModMessage = true;
    m_currentSubState = STATE_WASP_TRANSITION_IN;
    m_itemsComplete->ResetTransformation();
    m_itemsComplete->SetAlpha( 1.0f );
    m_itemsComplete->SetIndex( m_modMessageIndex );
}
#endif

void
HudWaspDestroyed::Stop()
{
    this->OnStop();
}

void
HudWaspDestroyed::Update( float elapsedTime )
{
    if( m_currentState == STATE_RUNNING )
    {
        m_elapsedTime += elapsedTime;

        switch( m_currentSubState )
        {
            case STATE_WASP_TRANSITION_IN:
            {
                const float CARD_TRANSITION_IN_TIME = 300.0f;

                m_itemsComplete->ResetTransformation();

                if( m_elapsedTime < CARD_TRANSITION_IN_TIME )
                {
                    float percentageDone = m_elapsedTime / CARD_TRANSITION_IN_TIME;

                    // scale up card deck complete text
                    //
                    m_itemsComplete->ScaleAboutCenter( percentageDone );

                    // apply projectile motion effect to card deck complete text
                    //
                    const float TEXT_GRAVITY = 0.005f;
                    GuiSFX::Projectile( m_itemsComplete,
                                        m_elapsedTime,
                                        CARD_TRANSITION_IN_TIME,
                                        rmt::Vector( 0.0f, 0.0f, 0.0f ),
                                        rmt::Vector( 0.0f, 0.0f, 0.0f ),
                                        false,
                                        TEXT_GRAVITY );
                }
                else
                {
                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                break;
            }
            case STATE_WASP_DISPLAY_HOLD:
            {
                const float CARD_DISPLAY_HOLD_TIME = 2000.0f;

                if( m_elapsedTime < CARD_DISPLAY_HOLD_TIME )
                {
                    // do nothing
                }
                else
                {
                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                break;
            }
            case STATE_WASP_TRANSITION_OUT:
            {
                const float CARD_TRANSITION_OUT_TIME = 300.0f;

                if( m_elapsedTime < CARD_TRANSITION_OUT_TIME )
                {
                    float percentageDone = m_elapsedTime / CARD_TRANSITION_OUT_TIME;

                    // fade out card text (and scale up)
                    //
                    rAssert( m_itemsComplete != NULL );
                    m_itemsComplete->SetAlpha( 1.0f - percentageDone );

                    m_itemsComplete->ResetTransformation();
                    m_itemsComplete->ScaleAboutCenter( 1.0f + percentageDone * 0.5f );
                }
                else
                {
                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                break;
            }
            case NUM_SUB_STATES:
            {
                // ok, we're done
                //
                this->Stop();

                break;
            }
            default:
            {
                rAssertMsg( false, "Unhandled sub-state!" );
                break;
            }
        }
    }
}

