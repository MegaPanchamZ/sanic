using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Ability state
/// </summary>
public enum AbilityState
{
    Ready = 0,
    Active = 1,
    Cooldown = 2
}

/// <summary>
/// Built-in ability types
/// </summary>
public enum AbilityType
{
    Boost = 0,
    SuperJump = 1,
    ZiplineAttach = 2,
    Dash = 3,
    GroundPound = 4
}

/// <summary>
/// Base class for character abilities.
/// Uses a lightweight Gameplay Ability System inspired pattern.
/// </summary>
public abstract class Ability
{
    // Native imports
    [DllImport("sanic_native")]
    private static extern uint Ability_Grant(uint entityId, int abilityType);
    
    [DllImport("sanic_native")]
    private static extern void Ability_Revoke(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern bool Ability_CanActivate(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern void Ability_Activate(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern void Ability_Deactivate(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern bool Ability_IsActive(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern int Ability_GetState(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern float Ability_GetCooldownRemaining(uint entityId, uint abilityId);
    
    [DllImport("sanic_native")]
    private static extern void Ability_SetCooldown(uint entityId, uint abilityId, float cooldown);
    
    [DllImport("sanic_native")]
    private static extern void Ability_SetResourceCost(uint entityId, uint abilityId, float cost);

    protected uint EntityId { get; private set; }
    protected uint AbilityId { get; private set; }

    /// <summary>
    /// Current ability state
    /// </summary>
    public AbilityState State => (AbilityState)Ability_GetState(EntityId, AbilityId);

    /// <summary>
    /// Whether ability is ready to activate
    /// </summary>
    public bool IsReady => State == AbilityState.Ready;

    /// <summary>
    /// Whether ability is currently active
    /// </summary>
    public bool IsActive => State == AbilityState.Active;

    /// <summary>
    /// Whether ability is on cooldown
    /// </summary>
    public bool IsOnCooldown => State == AbilityState.Cooldown;

    /// <summary>
    /// Remaining cooldown time in seconds
    /// </summary>
    public float CooldownRemaining => Ability_GetCooldownRemaining(EntityId, AbilityId);

    /// <summary>
    /// Ability cooldown duration
    /// </summary>
    public float Cooldown
    {
        set => Ability_SetCooldown(EntityId, AbilityId, value);
    }

    /// <summary>
    /// Resource (energy/stamina) cost
    /// </summary>
    public float ResourceCost
    {
        set => Ability_SetResourceCost(EntityId, AbilityId, value);
    }

    /// <summary>
    /// Whether this ability can be activated right now
    /// </summary>
    public bool CanActivate() => Ability_CanActivate(EntityId, AbilityId);

    /// <summary>
    /// Attempt to activate this ability
    /// </summary>
    public bool TryActivate()
    {
        if (CanActivate())
        {
            Ability_Activate(EntityId, AbilityId);
            return true;
        }
        return false;
    }

    /// <summary>
    /// Force activate (ignores CanActivate check)
    /// </summary>
    public void ForceActivate() => Ability_Activate(EntityId, AbilityId);

    /// <summary>
    /// Deactivate this ability
    /// </summary>
    public void Deactivate() => Ability_Deactivate(EntityId, AbilityId);

    /// <summary>
    /// Grant this ability type to an entity
    /// </summary>
    internal void Initialize(uint entityId, AbilityType type)
    {
        EntityId = entityId;
        AbilityId = Ability_Grant(entityId, (int)type);
    }

    /// <summary>
    /// Revoke this ability
    /// </summary>
    public void Revoke() => Ability_Revoke(EntityId, AbilityId);
}

/// <summary>
/// Forward velocity boost ability
/// </summary>
public class BoostAbility : Ability
{
    [DllImport("sanic_native")]
    private static extern void BoostAbility_SetParameters(uint entityId, uint abilityId, float force, float duration);

    private float _force = 500f;
    private float _duration = 0.5f;

    /// <summary>
    /// Boost force strength
    /// </summary>
    public float Force
    {
        get => _force;
        set
        {
            _force = value;
            UpdateParameters();
        }
    }

    /// <summary>
    /// Boost duration in seconds
    /// </summary>
    public float Duration
    {
        get => _duration;
        set
        {
            _duration = value;
            UpdateParameters();
        }
    }

    private void UpdateParameters()
    {
        BoostAbility_SetParameters(EntityId, AbilityId, _force, _duration);
    }

    /// <summary>
    /// Grant boost ability to an entity
    /// </summary>
    public static BoostAbility Grant(uint entityId, float force = 500f, float duration = 0.5f)
    {
        var ability = new BoostAbility { _force = force, _duration = duration };
        ability.Initialize(entityId, AbilityType.Boost);
        ability.UpdateParameters();
        return ability;
    }
}

/// <summary>
/// Charged super jump ability
/// </summary>
public class SuperJumpAbility : Ability
{
    [DllImport("sanic_native")]
    private static extern void SuperJumpAbility_SetParameters(uint entityId, uint abilityId, 
        float minForce, float maxForce, float chargeTime);

    private float _minForce = 100f;
    private float _maxForce = 500f;
    private float _chargeTime = 1f;

    /// <summary>
    /// Minimum jump force (no charge)
    /// </summary>
    public float MinForce
    {
        get => _minForce;
        set
        {
            _minForce = value;
            UpdateParameters();
        }
    }

    /// <summary>
    /// Maximum jump force (full charge)
    /// </summary>
    public float MaxForce
    {
        get => _maxForce;
        set
        {
            _maxForce = value;
            UpdateParameters();
        }
    }

    /// <summary>
    /// Time to fully charge in seconds
    /// </summary>
    public float ChargeTime
    {
        get => _chargeTime;
        set
        {
            _chargeTime = value;
            UpdateParameters();
        }
    }

    private void UpdateParameters()
    {
        SuperJumpAbility_SetParameters(EntityId, AbilityId, _minForce, _maxForce, _chargeTime);
    }

    /// <summary>
    /// Grant super jump ability to an entity
    /// </summary>
    public static SuperJumpAbility Grant(uint entityId, float minForce = 100f, float maxForce = 500f, float chargeTime = 1f)
    {
        var ability = new SuperJumpAbility { _minForce = minForce, _maxForce = maxForce, _chargeTime = chargeTime };
        ability.Initialize(entityId, AbilityType.SuperJump);
        ability.UpdateParameters();
        return ability;
    }
}

/// <summary>
/// Zipline attachment ability
/// </summary>
public class ZiplineAttachAbility : Ability
{
    /// <summary>
    /// Grant zipline attach ability to an entity
    /// </summary>
    public static ZiplineAttachAbility Grant(uint entityId, float detectionRadius = 5f)
    {
        var ability = new ZiplineAttachAbility();
        ability.Initialize(entityId, AbilityType.ZiplineAttach);
        return ability;
    }
}

/// <summary>
/// Quick dash ability
/// </summary>
public class DashAbility : Ability
{
    [DllImport("sanic_native")]
    private static extern void DashAbility_SetParameters(uint entityId, uint abilityId,
        float distance, float duration, float cooldown);

    private float _distance = 10f;
    private float _duration = 0.2f;
    private float _cooldown = 1f;

    /// <summary>
    /// Dash distance
    /// </summary>
    public float Distance
    {
        get => _distance;
        set
        {
            _distance = value;
            UpdateParameters();
        }
    }

    /// <summary>
    /// Dash duration in seconds
    /// </summary>
    public float Duration
    {
        get => _duration;
        set
        {
            _duration = value;
            UpdateParameters();
        }
    }

    /// <summary>
    /// Cooldown override
    /// </summary>
    public float CooldownDuration
    {
        get => _cooldown;
        set
        {
            _cooldown = value;
            UpdateParameters();
        }
    }

    private void UpdateParameters()
    {
        DashAbility_SetParameters(EntityId, AbilityId, _distance, _duration, _cooldown);
    }

    /// <summary>
    /// Grant dash ability to an entity
    /// </summary>
    public static DashAbility Grant(uint entityId, float distance = 10f, float duration = 0.2f, float cooldown = 1f)
    {
        var ability = new DashAbility { _distance = distance, _duration = duration, _cooldown = cooldown };
        ability.Initialize(entityId, AbilityType.Dash);
        ability.UpdateParameters();
        return ability;
    }
}

/// <summary>
/// Ground pound ability
/// </summary>
public class GroundPoundAbility : Ability
{
    /// <summary>
    /// Grant ground pound ability to an entity
    /// </summary>
    public static GroundPoundAbility Grant(uint entityId)
    {
        var ability = new GroundPoundAbility();
        ability.Initialize(entityId, AbilityType.GroundPound);
        return ability;
    }
}
