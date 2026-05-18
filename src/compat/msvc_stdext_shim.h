#pragma once
//
// MSVC <-> Qt 5.15.2 호환 shim.
//
// Qt 5.15.2 의 qcompilerdetection.h 는 MSVC 에서 무조건
//   stdext::make_checked_array_iterator / make_unchecked_array_iterator
// 를 사용하도록 매크로를 정의한다. 그러나 MSVC 14.4x+ (VS 2022 17.10+,
// VS 2026 / MSVC 19.51) 에서는 이 stdext 체크드 배열 이터레이터들이
// STL 에서 완전히 제거되어 C2065 'stdext' 미선언 오류가 발생한다.
//
// 이 헤더는 해당 팩토리들을 raw 포인터를 반환하는 형태로 최소 구현하여
// 제공한다. Qt 내부에서는 std::copy / std::uninitialized_copy 의 출력
// 이터레이터로만 쓰이므로 contiguous 포인터 반환으로 의미가 동일하다.
//
// CMake 에서 MSVC 전용 force-include(/FI) 로 모든 번역 단위(autogen 포함)
// 의 최상단에 주입된다.
//
#if defined(_MSC_VER)

#include <cstddef>

namespace stdext {

template <class T>
constexpr T* make_unchecked_array_iterator(T* ptr) noexcept {
    return ptr;
}

template <class T>
constexpr T* make_checked_array_iterator(T* ptr,
                                         std::size_t /*size*/,
                                         std::ptrdiff_t offset = 0) noexcept {
    return ptr + offset;
}

} // namespace stdext

#endif // _MSC_VER
