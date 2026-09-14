#include "CorePch.h"
#include "FaultInjection.h"

#include <string_view>

std::atomic<int32> FaultInjection::sRemaining[static_cast<int>(FaultPoint::Count)];

namespace
{
	constexpr const char* kFaultPointNames[] = { "PacketHandler", "JobExecute", "DbJob", "CoroutineBody" };
	static_assert(std::size(kFaultPointNames) == static_cast<size_t>(FaultPoint::Count),
				  "FaultPoint 와 kFaultPointNames 의 개수가 다릅니다");
}

void FaultInjection::Init() noexcept
{
#ifdef _DEBUG
	char spec[256] = {};
	const DWORD len = ::GetEnvironmentVariableA("MMO_FAULT", spec, static_cast<DWORD>(sizeof(spec)));
	if (len == 0)
		return;
	if (len >= sizeof(spec))
	{
		MLOG_WARN(Sys) << "[FaultInjection] MMO_FAULT is too long (" << len << " chars). Ignored.";
		return;
	}

	// 형식: 이름:횟수[,이름:횟수...]  (횟수 생략 시 1)
	std::string_view rest(spec, len);
	while (!rest.empty())
	{
		const size_t comma = rest.find(',');
		const std::string_view item = rest.substr(0, comma);
		rest = (comma == std::string_view::npos) ? std::string_view() : rest.substr(comma + 1);

		const size_t colon = item.find(':');
		const std::string_view name = item.substr(0, colon);
		const int32 count = (colon == std::string_view::npos) ? 1 : std::atoi(std::string(item.substr(colon + 1)).c_str());

		int index = -1;
		for (int i = 0; i < static_cast<int>(FaultPoint::Count); ++i)
		{
			if (name == kFaultPointNames[i])
				index = i;
		}
		if (index < 0 || count <= 0)
		{
			MLOG_WARN(Sys) << "[FaultInjection] Ignored MMO_FAULT item: " << item;
			continue;
		}
		sRemaining[index].store(count);
		MLOG_INFO(Sys) << "[FaultInjection] Armed " << kFaultPointNames[index] << " x" << count;
	}
#endif
}

bool FaultInjection::ShouldFire(FaultPoint p) noexcept
{
	std::atomic<int32>& remaining = sRemaining[static_cast<int>(p)];
	int32 current = remaining.load();
	while (current > 0)
	{
		if (remaining.compare_exchange_weak(current, current - 1))
			return true;
	}
	return false;
}
