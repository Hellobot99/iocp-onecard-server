#pragma once

#include <string>

// 비밀번호를 salt + SHA-256으로 저장하기 위한 유틸리티. 외부 라이브러리 없이
// Windows CNG(BCrypt) API만으로 구현한다.
//
// 주의: SHA-256은 빠른 해시라서 브루트포스/레인보우테이블 공격에 bcrypt/Argon2
// 같은 전용 비밀번호 해시 알고리즘보다 약하다. 실서비스라면 bcrypt/Argon2/scrypt를
// 써야 하지만, 이 프로젝트는 외부 크립토 라이브러리 추가 없이 Windows 내장 API로
// "평문 저장은 절대 안 한다"는 원칙을 보여주는 데 목적을 두고 SHA-256을 택했다.
namespace PasswordHash
{
    // 계정마다 고유한 랜덤 salt를 16바이트(32자 hex 문자열)로 생성한다.
    std::string GenerateSaltHex();

    // SHA-256(salt + password)를 64자 hex 문자열로 반환한다.
    std::string Hash(const std::string &password, const std::string &saltHex);

    // 입력 비밀번호가 저장된 salt/hash와 일치하는지 확인한다.
    bool Verify(const std::string &password, const std::string &saltHex, const std::string &expectedHashHex);
}
