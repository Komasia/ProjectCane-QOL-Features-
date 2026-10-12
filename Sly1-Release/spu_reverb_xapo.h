#pragma once

#ifdef _WIN32
#include <xapo.h>
#endif
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#ifdef _WIN32
// SPU2 hardware reverb (the PS2 sound chip's effect unit), run as an XAudio2
// effect. The register presets come from the PS2 sound library (libsd); the
// game selects one per world with snd_SetReverbType. Mode numbers are libsd's
// SD_EFFECT_MODE_* values: 1 Room, 2 Studio A, 3 Studio B, 4 Studio C, 9 Pipe.
class SpuStudioCReverb final : public IXAPO
{
public:
	SpuStudioCReverb();

	// Select the libsd preset and clear the work area, as sceSdSetEffectAttr does.
	void SetMode(int sdMode);

	STDMETHOD(QueryInterface)(REFIID interfaceId, void** result) override;
	STDMETHOD_(ULONG, AddRef)() override;
	STDMETHOD_(ULONG, Release)() override;
	STDMETHOD(GetRegistrationProperties)(XAPO_REGISTRATION_PROPERTIES** properties) override;
	STDMETHOD(IsInputFormatSupported)(const WAVEFORMATEX* outputFormat,
		const WAVEFORMATEX* requestedInputFormat, WAVEFORMATEX** supportedInputFormat) override;
	STDMETHOD(IsOutputFormatSupported)(const WAVEFORMATEX* inputFormat,
		const WAVEFORMATEX* requestedOutputFormat, WAVEFORMATEX** supportedOutputFormat) override;
	STDMETHOD(Initialize)(const void* data, UINT32 dataSize) override;
	STDMETHOD_(void, Reset)() override;
	STDMETHOD(LockForProcess)(UINT32 inputCount,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* inputs, UINT32 outputCount,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* outputs) override;
	STDMETHOD_(void, UnlockForProcess)() override;
	STDMETHOD_(void, Process)(UINT32 inputCount,
		const XAPO_PROCESS_BUFFER_PARAMETERS* inputs, UINT32 outputCount,
		XAPO_PROCESS_BUFFER_PARAMETERS* outputs, BOOL enabled) override;
	STDMETHOD_(UINT32, CalcInputFrames)(UINT32 outputFrames) override;
	STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 inputFrames) override;

private:
	static constexpr int kTapCount = 39;
	static constexpr int kRingSize = 64;
	static constexpr int kMaxEffectBufferSize = 0x1804 * 8; // largest libsd work area, in samples
	struct Params
	{
		int apf1Size = 0, apf2Size = 0;
		int vIir = 0, vComb1 = 0, vComb2 = 0, vComb3 = 0, vComb4 = 0, vWall = 0, vApf1 = 0, vApf2 = 0;
		int sameDst[2]{}, comb1[2]{}, comb2[2]{}, sameSrc[2]{}, diffDst[2]{}, comb3[2]{}, comb4[2]{};
		int diffSrc[2]{}, apf1Dst[2]{}, apf2Dst[2]{};
		int vIn[2]{};
	};
	void LoadModeLocked(int sdMode);
	static int Clamp16(int64_t value);
	static int64_t MulQ15(int64_t left, int64_t right);
	int EffectIndex(int offset) const;
	int Downsample(bool right) const;
	std::array<int, 2> Upsample() const;
	std::array<int, 2> ProcessFrame(int left, int right);

	std::vector<short> m_effectBuffer;
	int m_effectBufferSize = 1;
	int m_mode = 4;
	Params m_params{};
	std::array<std::array<short, kRingSize * 2>, 2> m_down{};
	std::array<std::array<short, kRingSize * 2>, 2> m_up{};
	std::atomic<ULONG> m_references{1};
	std::mutex m_processMutex;
	bool m_locked = false;
	unsigned m_ringPosition = 0;
	unsigned long long m_cycle = 0;
};
#else
class SpuStudioCReverb
{
public:
	void Reset() {}
	void SetMode(int) {}
};
#endif
