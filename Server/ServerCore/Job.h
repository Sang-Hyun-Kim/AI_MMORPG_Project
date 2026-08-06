#pragma once

#include "CorePch.h"

/*--------------
	Job
---------------*/

using CallbackType = std::function<void()>;

class Job
{
public:
	Job(CallbackType&& callback) : _callback(std::move(callback)) {}
	
	template<typename T, typename Ret, typename... Args>
	Job(std::shared_ptr<T> owner, Ret(T::* memFunc)(Args...), Args&&... args)
	{
		// C++20 lambda capture (owner는 복사 캡처로 생명주기 유지)
		_callback = [owner, memFunc, args = std::make_tuple(std::forward<Args>(args)...)]() mutable
		{
			std::apply([owner, memFunc](auto&&... unpackedArgs)
			{
				(owner.get()->*memFunc)(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
			}, std::move(args));
		};
	}

	void Execute()
	{
		if (_callback)
			_callback();
	}

private:
	CallbackType _callback;
};

using JobRef = std::shared_ptr<Job>;
