//===========================================================================
// Component:   CGuiScreenGraphics
//
// Description: Implementation of the CGuiScreenGraphics class (Linux port).
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/frontend/guiscreengraphics.h>
#include <presentation/gui/guimenu.h>

#include <data/config/gameconfigmanager.h>
#include <main/win32platform.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp> // Foundation
#include <Screen.h>
#include <Page.h>
#include <Group.h>
#include <Text.h>
#include <Sprite.h>

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

// the Display page rows this screen's items take over, in menu order
static const char* DISPLAY_ROWS[] =
{
    "Resolution",
    "ColourDepth",
    "DisplayMode"
};

static const char* GRAPHICS_LABELS[] =
{
    "Anti-Aliasing",
    "Shaders",
    "Renderer"
};

static const int ANTIALIASING_SAMPLES[] = { 0, 2, 4, 8 };
static const char* ANTIALIASING_VALUES[] = { "Off", "2x", "4x", "8x" };
static const int NUM_ANTIALIASING_VALUES = sizeof( ANTIALIASING_SAMPLES ) / sizeof( ANTIALIASING_SAMPLES[ 0 ] );

static const char* SHADERS_VALUES[] = { "Off", "On" };

static const char* RENDERER_VALUES[ Win32Platform::NUM_RENDERERS ] = { "OpenGL", "Vulkan" };

//===========================================================================
// AddGraphicsMenuItem
//===========================================================================
// Description: See guiscreengraphics.h (CGuiScreenOptions, CGuiScreenPauseOptions).
//
//===========================================================================
Scrooby::Text* AddGraphicsMenuItem( Scrooby::Group* menu, const char* after, const char* const* below, int numBelow )
{
    rAssert( menu != NULL && numBelow > 0 );
    Scrooby::Text* previous = menu->GetText( after );
    Scrooby::Text* next = menu->GetText( below[ 0 ] );
    rAssert( previous != NULL && next != NULL );

    int x0, y0, x1, y1;
    previous->GetOriginPosition( x0, y0 );
    next->GetOriginPosition( x1, y1 );
    int row = y1 - y0;

    // positions, not transforms: the menu resets its items' transforms (selection pulse)
    Scrooby::Text* graphics = previous->Clone( "Graphics", "GRAPHICS" );
    rAssert( graphics != NULL );
    graphics->SetPosition( x0, y0 + row - row / 2 );
    previous->SetPosition( x0, y0 - row / 2 );
    for( int i = 0; i < numBelow; i++ )
    {
        Scrooby::Text* text = menu->GetText( below[ i ] );
        if( text != NULL )
        {
            int x, y;
            text->GetOriginPosition( x, y );
            text->SetPosition( x, y + row - row / 2 );
        }
    }
    return graphics;
}

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenGraphics::CGuiScreenGraphics
//===========================================================================
// Description: Constructor.
//
//===========================================================================
CGuiScreenGraphics::CGuiScreenGraphics
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent,
    bool inGame
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_GRAPHICS ),
    m_pMenu( NULL ),
    m_inGame( inGame ),
    m_changed( false ),
    m_gamma( NULL ),
    m_applyChanges( NULL )
{
MEMTRACK_PUSH_GROUP( "CGuiScreenGraphics" );
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Display" );
    rAssert( pPage != NULL );

    m_pMenu = new CGuiMenu( this, NUM_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    char itemName[ 32 ];
    for( int i = 0; i < NUM_MENU_ITEMS; i++ )
    {
        Scrooby::Group* group = pPage->GetGroup( DISPLAY_ROWS[ i ] );
        rAssert( group != NULL );

        m_displayLabels[ i ] = group->GetText( DISPLAY_ROWS[ i ] );
        sprintf( itemName, "%s_Value", DISPLAY_ROWS[ i ] );
        m_displayValues[ i ] = group->GetText( itemName );
        rAssert( m_displayLabels[ i ] != NULL && m_displayValues[ i ] != NULL );

        // shared with the Display screen's menu, which already scaled them (AddMenuItem would again)
        sprintf( itemName, "%s_ArrowL", DISPLAY_ROWS[ i ] );
        m_arrows[ i ][ 0 ] = group->GetSprite( itemName );
        sprintf( itemName, "%s_ArrowR", DISPLAY_ROWS[ i ] );
        m_arrows[ i ][ 1 ] = group->GetSprite( itemName );
        rAssert( m_arrows[ i ][ 0 ] != NULL && m_arrows[ i ][ 1 ] != NULL );
        rmt::Matrix arrowTransforms[ 2 ] = { *m_arrows[ i ][ 0 ]->GetTransform(), *m_arrows[ i ][ 1 ]->GetTransform() };

        const char* const* values = NULL;
        int numValues = 0;
        switch( i )
        {
            case MENU_ITEM_ANTIALIASING:
            {
                values = ANTIALIASING_VALUES;
                numValues = NUM_ANTIALIASING_VALUES;
                break;
            }
            case MENU_ITEM_SHADERS:
            {
                values = SHADERS_VALUES;
                numValues = 2;
                break;
            }
            case MENU_ITEM_RENDERER:
            {
                values = RENDERER_VALUES;
                numValues = Win32Platform::NUM_RENDERERS;
                break;
            }
        }

        sprintf( itemName, "Graphics%d", i );
        m_labels[ i ] = m_displayLabels[ i ]->Clone( itemName, GRAPHICS_LABELS[ i ] );
        sprintf( itemName, "Graphics%d_Value", i );
        m_values[ i ] = m_displayValues[ i ]->Clone( itemName, values[ 0 ] );
        rAssert( m_labels[ i ] != NULL && m_values[ i ] != NULL );
        for( int v = 1; v < numValues; v++ )
        {
            m_values[ i ]->AddHardCodedString( values[ v ] );
        }
        m_labels[ i ]->SetVisible( false );
        m_values[ i ]->SetVisible( false );

        m_pMenu->AddMenuItem( m_labels[ i ],
                              m_values[ i ],
                              NULL,
                              NULL,
                              m_arrows[ i ][ 0 ],
                              m_arrows[ i ][ 1 ],
                              SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );

        m_arrows[ i ][ 0 ]->SetTransform( arrowTransforms[ 0 ] );
        m_arrows[ i ][ 1 ]->SetTransform( arrowTransforms[ 1 ] );
    }

    m_gamma = pPage->GetGroup( "Gamma" );
    Scrooby::Group* menu = pPage->GetGroup( "Menu" );
    if( menu != NULL )
    {
        m_applyChanges = menu->GetText( "ApplyChanges" );
    }
MEMTRACK_POP_GROUP( "CGuiScreenGraphics" );
}


//===========================================================================
// CGuiScreenGraphics::~CGuiScreenGraphics
//===========================================================================
// Description: Destructor (the copied texts belong to the page).
//
//===========================================================================
CGuiScreenGraphics::~CGuiScreenGraphics()
{
    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenGraphics::HandleMessage
//===========================================================================
void CGuiScreenGraphics::HandleMessage
(
	eGuiMessage message,
	GuiParam param1,
	GuiParam param2
)
{
    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        switch( message )
        {
            case GUI_MSG_CONTROLLER_START:
            {
                if( m_inGame )
                {
                    // resume game
                    m_pParent->HandleMessage( GUI_MSG_UNPAUSE_INGAME );
                }
                break;
            }
            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
                Win32Platform* plat = Win32Platform::GetInstance();
                int value = static_cast< int >( param2 );
                switch( param1 )
                {
                    case MENU_ITEM_ANTIALIASING:
                    {
                        if( value >= 0 && value < NUM_ANTIALIASING_VALUES )
                        {
                            plat->SetAntialiasing( ANTIALIASING_SAMPLES[ value ] );
                        }
                        break;
                    }
                    case MENU_ITEM_SHADERS:
                    {
                        plat->SetUserShaders( value != 0 );
                        break;
                    }
                    case MENU_ITEM_RENDERER:
                    {
                        // read at start-up
                        plat->SetRenderer( value == 1 ? Win32Platform::RENDERER_VULKAN : Win32Platform::RENDERER_OPENGL );
                        break;
                    }
                }
                m_changed = true;
                break;
            }
            default:
            {
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}


//===========================================================================
// CGuiScreenGraphics::InitIntro
//===========================================================================
void CGuiScreenGraphics::InitIntro()
{
    Win32Platform* plat = Win32Platform::GetInstance();

    int samples = plat->GetAntialiasing();
    int aa = 0;
    for( int k = 0; k < NUM_ANTIALIASING_VALUES; k++ )
    {
        if( ANTIALIASING_SAMPLES[ k ] <= samples )
        {
            aa = k;
        }
    }
    m_pMenu->SetSelectionValue( MENU_ITEM_ANTIALIASING, aa );
    m_pMenu->SetSelectionValue( MENU_ITEM_SHADERS, plat->GetUserShaders() ? 1 : 0 );
    m_pMenu->SetSelectionValue( MENU_ITEM_RENDERER, plat->GetRenderer() == Win32Platform::RENDERER_VULKAN ? 1 : 0 );

    this->ShowPage( true );
}


//===========================================================================
// CGuiScreenGraphics::InitRunning
//===========================================================================
void CGuiScreenGraphics::InitRunning()
{
}


//===========================================================================
// CGuiScreenGraphics::InitOutro
//===========================================================================
void CGuiScreenGraphics::InitOutro()
{
    if( m_changed )
    {
        GetGameConfigManager()->SaveConfigFile();
        m_changed = false;
    }
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

//===========================================================================
// CGuiScreenGraphics::ShowPage
//===========================================================================
// Description: See guiscreengraphics.h. The arrows are shared: hidden here, then shown for the selected item by
//              the menu that owns the screen (CGuiScreenDisplay::InitIntro resets its menu likewise).
//
//===========================================================================
void CGuiScreenGraphics::ShowPage( bool graphics )
{
    for( int i = 0; i < NUM_MENU_ITEMS; i++ )
    {
        m_displayLabels[ i ]->SetVisible( !graphics );
        m_displayValues[ i ]->SetVisible( !graphics );
        m_labels[ i ]->SetVisible( graphics );
        m_values[ i ]->SetVisible( graphics );
        m_arrows[ i ][ 0 ]->SetVisible( false );
        m_arrows[ i ][ 1 ]->SetVisible( false );
    }
    if( m_gamma != NULL )
    {
        m_gamma->SetVisible( !graphics );
    }
    if( m_applyChanges != NULL )
    {
        m_applyChanges->SetVisible( !graphics );
    }

    if( graphics )
    {
        m_pMenu->Reset( m_pMenu->GetSelection() );
    }
}
