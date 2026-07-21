#pragma once

#include <QString>

namespace uwp {

// P4 — 크래시 덤프 인프라 (Windows 전용).
//   SetUnhandledExceptionFilter 로 최상위 예외 필터 등록 → 미포착 예외 발생 시
//   MiniDumpWriteDump 로 dump 파일 생성. 크래시 원인 사후 분석 목적.
//
//   덤프 저장 경로: <dumpDir>/uWeddingPlayer_YYYYMMDD_HHmmss.dmp
//   보통 <appDir>/data/crash 를 사용 — 로그와 같은 위치 계열.
//
// 사용법: main() 아주 초기(QApplication 생성 직후, initialize 전) 1회 호출.
// dumpDir 은 생성 실패 시 조용히 실패(로그만) — 없어도 앱 정상 기동.
void installCrashHandler(const QString& dumpDir);

} // namespace uwp
