# 📘 MySQL C API (`libmysqlclient`) 연동 가이드

본 문서는 C++ 게임 서버(GameServer)와 MySQL 간의 비동기 직접 통신(M9 아키텍처)을 구현하기 위해, MySQL C API 라이브러리를 프로젝트에 세팅하는 과정을 설명합니다.

## 1. 개요 (Overview)

MySQL과 통신하기 위해서는 MySQL이 제공하는 C 인터페이스인 `libmysqlclient` 라이브러리가 필요합니다. 
초기 계획에서는 Pre-built 바이너리를 직접 다운로드하여 링킹하려 했으나, 현재 프로젝트 내에 이미 패키지 매니저인 **`vcpkg`**가 세팅되어 있고 `protobuf`와 `redis++`를 관리하고 있음을 확인했습니다.

따라서, **가장 안전하고 일관된 빌드 환경을 제공하는 `vcpkg`를 통해 `libmysqlclient`를 설치하고 연동**하는 방식으로 진행합니다.

## 2. 설치 방법 (Installation)

### 2.1 vcpkg를 통한 `libmysql` 설치
프로젝트 루트 경로(`d:\Mydev\AI_MMORPG_Project`)에서 PowerShell 윈도우를 열고 다음 명령어를 실행합니다.

```powershell
.\vcpkg\vcpkg install libmysql:x64-windows
```

이 명령어는 다음 작업을 자동으로 수행합니다:
1. MySQL C 커넥터 소스코드 다운로드
2. 64비트 Windows(MSVC) 환경에 맞게 컴파일
3. `vcpkg\installed\x64-windows\include` 에 `mysql.h` 등의 헤더 파일 배치
4. `vcpkg\installed\x64-windows\lib` 에 `libmysql.lib` 배치
5. `vcpkg\installed\x64-windows\bin` 에 `libmysql.dll` 배치

> [!TIP]
> vcpkg가 전역 설정(Integration)되어 있다면, Visual Studio MSBuild(혹은 CMake)에서 라이브러리 및 헤더 경로를 자동으로 인식하므로 복잡한 프로젝트 설정(속성 -> C/C++ -> 일반 -> 추가 포함 디렉터리 등)을 생략할 수 있습니다.

### 2.2 CMake 연동 (프로젝트 내장 시)
`ServerCore` 의 `CMakeLists.txt` 파일에 `libmysql` 의존성을 명시해야 합니다.

```cmake
# CMakeLists.txt (예시)
find_package(unofficial-libmysql CONFIG REQUIRED)
target_link_libraries(ServerCore PUBLIC unofficial::libmysql::libmysql)
```
또는 vcpkg 툴체인이 알아서 처리해준다면 `#pragma comment(lib, "libmysql.lib")` 코드를 코드 베이스에 삽입할 수도 있습니다.

## 3. 코드 적용 (Code Integration)

라이브러리가 설치되면 `DBConnection` 클래스에서 다음과 같이 `mysql.h`를 인클루드하여 사용합니다.

```cpp
#include <mysql.h>

class DBConnection
{
public:
    bool Connect()
    {
        _conn = mysql_init(nullptr);
        if (_conn == nullptr) return false;
        
        // GConfigManager에서 읽어온 정보를 바탕으로 접속
        mysql_real_connect(_conn, host, user, pw, db, port, nullptr, 0);
        return true;
    }
private:
    MYSQL* _conn = nullptr;
};
```

## 4. 트러블 슈팅 (Troubleshooting)

- **에러**: `#include <mysql.h>` 를 찾을 수 없습니다.
  - **해결**: vcpkg 인테그레이션이 풀린 경우입니다. `.\vcpkg\vcpkg integrate install`을 실행하세요.
- **에러**: 프로그램 실행 시 `libmysql.dll`을 찾을 수 없다는 시스템 오류 팝업 발생
  - **해결**: `libmysql.dll` 파일이 `GameServer.exe` 가 위치한 실행 폴더(OutDir)에 복사되어야 합니다. vcpkg는 보통 컴파일 후 자동으로 dll을 복사해주지만, 만약 실패한다면 `vcpkg\installed\x64-windows\bin\libmysql.dll`을 수동으로 복사해주세요.
