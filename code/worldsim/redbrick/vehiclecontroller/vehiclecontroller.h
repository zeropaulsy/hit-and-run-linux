#ifndef VEHICLECONTROLER_H_
#define VEHICLECONTROLER_H_

#include <p3d/refcounted.hpp>
#include <input/button.h>

class Vehicle;

/*
// Skeleton class for example.
//
struct IButton
{
    virtual float GetValue( void ) const = 0;
    virtual float GetHeldTime( void ) const = 0;
};
*/
class VehicleController
:
public tRefCounted
{
public:
	VehicleController( void );
	virtual ~VehicleController( void );
    
    virtual void Update( float timeins ) {};
    virtual void Init() {};
    virtual void Shutdown() {};

    void SetVehicle( Vehicle* pVehicle ) { mpVehicle = pVehicle; }

	virtual float GetGas( void ) const = 0;
    virtual float GetThrottle( void ) const = 0;
    
	virtual float GetBrake( void ) const = 0;
    virtual float GetSteering( bool& isWheel ) const = 0;
    virtual float GetSteerLeft( void ) const = 0;
    virtual float GetSteerRight( void ) const = 0;
#ifdef RAD_LINUX
    // Linux port: the steering the car gets (VehicleCentral: the stick, or the keys/d-pad when they are further
    // over), -1 left .. 1 right
    float GetEffectiveSteering( void ) const
    {
        bool isWheel;
        float stick = GetSteering( isWheel );
        float left = GetSteerLeft();
        float right = GetSteerRight();
        float absStick = stick < 0.0f ? -stick : stick;
        if( absStick > left && absStick > right )
        {
            return stick;
        }
        return right > left ? right : -left;
    }
#endif

	virtual float GetHandBrake( void ) const = 0;
	virtual float GetReverse( void ) const = 0;
	virtual float GetHorn( void ) const = 0;

    Vehicle* GetVehicle( void ) const;
private:
	Vehicle* mpVehicle;
};

#endif // VEHICLECONTROLER_H_