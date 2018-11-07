#pragma once

#pragma warning(push, 0)
#pragma warning(pop)

namespace flex
{
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

		real kp = 10.0f;
		real kd = 3.5f;

		T targetPos;
		T targetVel;
		T pos;
		T vel;
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
		T fSpring = kp * (targetPos - pos);
		T fDamp = kd * (targetVel - vel);

		pos += vel * dt;
		vel += (fSpring + fDamp) * dt;
	}

} // namespace flex
