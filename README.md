# Taiko Key WASAPI

[Taiko-Key-ASIO](https://github.com/4dblackhole/Taiko-Key-ASIO)의 **비공식 WASAPI 카피 버전**입니다. 원본 UI·동/캇 음원을 그대로 사용하고, 입력·오디오 엔진을 C++20 / Raw Input / WASAPI 저지연 공유 모드로 새로 구현했습니다.

Windows 10/11 x64 지원. 별도 ASIO·FlexASIO·FMOD 설치 없이 게임 음악과 함께 타격음을 재생하며, 키·음원·출력 장치·볼륨 설정과 연타·동시 입력을 지원합니다.

## 사용

`TaikoKeyWASAPI.exe`를 실행하면 바로 동작합니다. 기본 키는 **Z / 슬래시(/) = 캇, X / 마침표(.) = 동**이며, osu!의 타격 효과음은 직접 꺼 주세요. 최소화해도 동작하며 메인 창을 닫으면 종료됩니다.

**Device**는 출력 장치, **Volume**은 볼륨, **Settings**는 상세 설정입니다. 키·음원은 `KeyBind.ini`를 편집한 뒤 **Reload ini File**로 적용합니다.

[Releases](https://github.com/poca-p0ca/Taiko-Key-WASAPI/releases) · [키 설정](docs/KEY_NAMES.md)

## 실측

**FMOD ASIO + FlexASIO 대비 중앙값 11.07ms, 약 29% 단축.**

| 구현 | 타격 수 | 중앙값 | p95 |
| --- | ---: | ---: | ---: |
| 원본 FMOD ASIO + FlexASIO | 108 | 38.69ms | 43.36ms |
| WASAPI 저지연 공유 | 108 | **27.62ms** | **31.61ms** |

동일 PC·Realtek 출력·48kHz에서 측정했으며 공유 엔진 주기는 모두 10ms였습니다. FlexASIO도 WASAPI 공유 모드로 설정했습니다. p95는 타격의 95%가 해당 지연 이내였다는 뜻입니다.

측정 범위는 **Raw Input 수신 → WASAPI 루프백의 소리 시작**이며 귀까지의 전체 지연은 아닙니다. UI 변경 전 버전의 단일 PC 결과로, 장치마다 차이가 있습니다.

별도의 프로젝트 라이선스는 지정하지 않습니다. [서드파티 출처·라이선스](THIRD_PARTY_NOTICES.md)
