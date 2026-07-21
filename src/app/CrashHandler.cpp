#include "CrashHandler.h"

#include <QDebug>
#include <QDir>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace uwp {

#ifdef _WIN32

namespace {

// 크래시 핸들러 내부에서는 heap 할당·Qt 함수 호출을 최소화해야 한다.
// (크래시 시점의 heap 은 이미 손상됐을 수 있음) — 파일 경로는 미리 확정된
// widechar 버퍼로 보관.
wchar_t g_dumpDirW[MAX_PATH] = { 0 };
size_t  g_dumpDirLen         = 0;

// 크래시 핸들러 — SetUnhandledExceptionFilter 등록용.
//   * 안전한 Win32 API 만 사용 (CRT/malloc/Qt 회피)
//   * dump 파일명: <dumpDir>\uWeddingPlayer_YYYYMMDD_HHmmss.dmp
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    if (g_dumpDirLen == 0) return EXCEPTION_CONTINUE_SEARCH;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t path[MAX_PATH];
    const int written = wsprintfW(
        path,
        L"%s\\uWeddingPlayer_%04d%02d%02d_%02d%02d%02d.dmp",
        g_dumpDirW,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    if (written <= 0) return EXCEPTION_CONTINUE_SEARCH;

    HANDLE hFile = CreateFileW(
        path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    mdei.ThreadId          = GetCurrentThreadId();
    mdei.ExceptionPointers = ep;
    mdei.ClientPointers    = FALSE;

    // MiniDumpWithDataSegs: 스택 + 데이터 세그먼트(전역/정적 변수) 포함.
    //   MiniDumpNormal 보다 크지만 원인 분석 정보 많음. 크래시 dump 는 자주
    //   생기지 않으므로 파일 크기 비용 감수(~수 MB 예상).
    MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(),
        hFile, MiniDumpWithDataSegs, &mdei, nullptr, nullptr);
    CloseHandle(hFile);

    // Search 로 리턴하면 기본 Windows 크래시 대화상자가 계속 뜨거나
    // WER 로 전달됨. EXECUTE_HANDLER 로 종료해 조용히 exit — 웨딩홀
    // 운용 중 크래시 대화상자가 LED 화면에 뜨는 참사 방지.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void installCrashHandler(const QString& dumpDir) {
    QDir().mkpath(dumpDir);   // 없으면 생성 (best-effort)

    // 절대 경로 확정. 크래시 시 CWD 가 바뀌었을 가능성 대비.
    const QString abs = QDir(dumpDir).absolutePath();
    const std::wstring wide = abs.toStdWString();
    if (wide.size() >= MAX_PATH) {
        qWarning() << "CrashHandler: dumpDir too long, dumps disabled —" << abs;
        return;
    }
    wcsncpy_s(g_dumpDirW, wide.c_str(), _TRUNCATE);
    g_dumpDirLen = wide.size();

    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    qInfo() << "CrashHandler: installed — dumps →" << abs;
}

#else   // 비-Windows: 노옵.

void installCrashHandler(const QString& /*dumpDir*/) {
    qInfo() << "CrashHandler: non-Windows platform — skipped";
}

#endif

} // namespace uwp
