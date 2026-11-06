//=============================================================================
// Linux port: aspect-ratio support.
//
// The game was laid out for 4:3. The window may have any shape (it can be
// resized), so:
//  - 3D cameras asking for the 4:3 design aspect keep its vertical view and
//    widen horizontally (Hor+), see tCamera::SetFOV;
//  - the 2D UI (640x480 Scrooby pages) is drawn in the largest centred 4:3
//    area, with GetDesignScaleX/Y giving that area as a fraction of the window,
//    and clipped to it (GetDesignRect), as some elements wait just off the page.
//=============================================================================

#ifndef _DISPLAYASPECT_HPP
#define _DISPLAYASPECT_HPP

#include <p3d/utility.hpp>
#include <pddi/pddi.hpp>

namespace p3d
{
    const float DESIGN_ASPECT = 4.0f / 3.0f;

    // Width / height of the window right now
    inline float GetDisplayAspect()
    {
        int w = display ? display->GetWidth() : 0;
        int h = display ? display->GetHeight() : 0;
        return ( w > 0 && h > 0 ) ? (float) w / (float) h : DESIGN_ASPECT;
    }

    // The centred 4:3 UI area as a fraction of the window (1 on the axis it fills)
    inline float GetDesignScaleX()
    {
        float a = GetDisplayAspect();
        return ( a > DESIGN_ASPECT ) ? DESIGN_ASPECT / a : 1.0f;
    }

    inline float GetDesignScaleY()
    {
        float a = GetDisplayAspect();
        return ( a < DESIGN_ASPECT ) ? a / DESIGN_ASPECT : 1.0f;
    }

    // simpsons.ini widehud=yes: the HUD elements anchored to a window edge (Scrooby::Drawable::SetEdgeAnchor) move out
    // to it on a window wider than 4:3 (left and right), or taller (top and bottom). Off by default: the HUD stays in
    // the centred 4:3 area.
    inline bool& WideHudEnabled()
    {
        static bool enabled = false;
        return enabled;
    }

    // How far an element anchored to a window edge moves out of the centred 4:3 area, so that it keeps its distance
    // from that edge, in widths (X) and heights (Y) of the 4:3 page: 0 along the axis the window is not wider (taller)
    // than 4:3 on, or when widehud is off
    inline float GetEdgeShift()
    {
        return WideHudEnabled() ? ( 1.0f / GetDesignScaleX() - 1.0f ) * 0.5f : 0.0f;
    }

    inline float GetEdgeShiftY()
    {
        return WideHudEnabled() ? ( 1.0f / GetDesignScaleY() - 1.0f ) * 0.5f : 0.0f;
    }

    // The centred 4:3 area in window pixels
    inline pddiRect GetDesignRect()
    {
        int w = display->GetWidth();
        int h = display->GetHeight();
        int x = (int) ( w * ( 1.0f - GetDesignScaleX() ) * 0.5f + 0.5f );
        int y = (int) ( h * ( 1.0f - GetDesignScaleY() ) * 0.5f + 0.5f );
        return pddiRect( x, y, w - x, h - y );
    }

    inline pddiRect GetDisplayRect()
    {
        return pddiRect( 0, 0, display->GetWidth(), display->GetHeight() );
    }
}

#endif
