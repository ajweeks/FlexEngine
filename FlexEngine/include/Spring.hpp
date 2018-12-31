#pragma once

namespace flex
{
	// See http://www.huwbowles.com/spring-dynamics-production/
	template<class T>
	class Spring
	{
	public:
		Spring();
		Spring(const T& targetPos);

		void Reset();
		void Tick(real dt);

		void SetTargetPos(const T& newTargetPos);
		void SetTargetVel(const T& newTargetVel);

		real DR = 0.9f; // Damping ratio (< 1 = overshoot, 1 = perfect)
		real UAF = 15.0f; // Undamped angular frequency (bounces per second)

		T targetPos;
		T targetVel;
		T pos;
		T vel;

	private:
		void TickImplementation(real dt);

	};

	template<class T>
	Spring<T>::Spring() :
		targetPos(T()),
		targetVel(T()),
		pos(T()),
		vel(T())
	{
	}

	template<class T>
	Spring<T>::Spring(const T& targetPos) :
		targetPos(targetPos),
		targetVel(T()),
		pos(T()),
		vel(T())
	{
	}

	template<class T>
	void Spring<T>::Reset()
	{
		targetPos = T();
		targetVel = T();
		pos = T();
		vel = T();
	}

	template<class T>
	void Spring<T>::SetTargetPos(const T& newTargetPos)
	{
		targetPos = newTargetPos;
	}

	template<class T>
	void Spring<T>::SetTargetVel(const T& newTargetVel)
	{
		targetVel = newTargetVel;
	}

	template<class T>
	void Spring<T>::Tick(real dt)
	{
#if 0 // No sub-stepping
		return TickImplementation(dt);
#else // Enforce max dt per sub-step to ensure framerate-independent behaviour
		const i32 maxUpdates = 8;
		const real maxDt = 1.0f / 60.0f;
		real stepCountF = glm::ceil(dt / maxDt);
		real substepDt = dt / stepCountF;

		i32 stepCount = (i32)stepCountF;
		for (i32 i = 0; i < stepCount; i++)
		{
			TickImplementation(substepDt);
		}
#endif
	}

	template<class T>
	void Spring<T>::TickImplementation(real dt)
	{
#if 0 // Cheap, semi-implicit Euler integration
		T fSpring = UAF * UAF * (targetPos - pos);
		T fDamp = 2.0f * DR * UAF * (targetVel - vel);

		vel += (fSpring + fDamp) * dt;
		pos += vel * dt;
#else // More expensive, implicit update (energy conserving, prevents divergence)
		T posNextState = pos + dt * vel;
		T fSpring = UAF * UAF * (targetPos - posNextState);
		T fDamp = 2.0f * DR * UAF * (targetVel - vel);

		pos += vel * dt;
		vel += (fSpring + fDamp) * dt / (1.0f + dt * 2.0f * DR * UAF);
#endif
	}

} // namespace flex
