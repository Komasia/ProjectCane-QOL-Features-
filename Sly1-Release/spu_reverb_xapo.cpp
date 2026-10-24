#include "spu_reverb_xapo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	constexpr std::array<short, 39> kDownFilter = {
		-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0,
		1332, 0, -2960, 0, 10246, 16384, 10246, 0, -2960, 0, 1332, 0,
		-616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1
	};

	// libsd (PS2 IOP sound library) effect presets, as reimplemented in
	// ps2sdk's iop/sound/libsd/src/effect.c. Each row is 16 u32 words holding
	// the 32 SPU2 effect registers (low half first). Work-area sizes are in
	// units of 8 samples. Index = SD_EFFECT_MODE_*.
	constexpr std::array<uint16_t, 10> kEffectSizes =
		{ 0x1, 0x26C, 0x1F4, 0x484, 0x6FE, 0xADE, 0xF6C, 0x1804, 0x1804, 0x3C0 };
	constexpr uint32_t kEffectParams[10][16] =
	{
		// OFF
		{ 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00010001, 0x00000000, 0x00000000,
		  0x00000000, 0x00010001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
		// ROOM
		{ 0x005B007D, 0x54B86D80, 0x0000BED0, 0xBA800000, 0x53005800, 0x033304D6, 0x022703F0, 0x01EF0374,
		  0x01B70336, 0x01B60335, 0x01B50334, 0x01B50334, 0x01B50334, 0x013601B4, 0x005C00B8, 0x80008000 },
		// STUDIO_1 (Studio A)
		{ 0x00250033, 0x4FA870F0, 0x4410BCE0, 0x9C00C0F0, 0x4EC05280, 0x031B03E4, 0x02AF03A4, 0x02660372,
		  0x025D031C, 0x018E025C, 0x0135022F, 0x00B701D2, 0x00B5018F, 0x008000B4, 0x0026004C, 0x80008000 },
		// STUDIO_2 (Studio B)
		{ 0x007F00B1, 0x4FA870F0, 0x4510BCE0, 0xB4C0BEF0, 0x4EC05280, 0x076B0904, 0x065F0824, 0x061607A2,
		  0x05ED076C, 0x042E05EC, 0x0305050F, 0x02B70462, 0x0265042F, 0x01B20264, 0x00800100, 0x80008000 },
		// STUDIO_3 (Studio C)
		{ 0x00A900E3, 0x4FA86F60, 0x4510BCE0, 0xA680BEF0, 0x52C05680, 0x0B580DFB, 0x0A3C0D09, 0x09730BD9,
		  0x08DA0B59, 0x05E908D9, 0x04B007EC, 0x03D206EF, 0x031D05EA, 0x0238031C, 0x00AA0154, 0x80008000 },
		// HALL
		{ 0x013901A5, 0x50006000, 0xB8004C00, 0xC000BC00, 0x5C006000, 0x11BB15BA, 0x10BD14C2, 0x0DC111BC,
		  0x0DC311C0, 0x09C10DC0, 0x07C10BC4, 0x06CD0A00, 0x05C109C2, 0x041A05C0, 0x013A0274, 0x80008000 },
		// SPACE
		{ 0x0231033D, 0x50007E00, 0xB000B400, 0xB0004C00, 0x54006000, 0x1A311ED6, 0x183B1D14, 0x16B21BC2,
		  0x15EF1A32, 0x105515EE, 0x0F2D1334, 0x0C5D11F6, 0x0AE11056, 0x07A20AE0, 0x02320464, 0x80008000 },
		// ECHO (default delay/feedback)
		{ 0x00030003, 0x7FFF7FFF, 0x00000000, 0x81000000, 0x00000000, 0x0FFD1FFD, 0x00091009, 0x00000000,
		  0x00091009, 0x1FFF1FFF, 0x1FFE1FFE, 0x1FFE1FFE, 0x1FFE1FFE, 0x10041008, 0x00040008, 0x80008000 },
		// DELAY (default delay)
		{ 0x00030003, 0x7FFF7FFF, 0x00000000, 0x00000000, 0x00000000, 0x0FFD1FFD, 0x00091009, 0x00000000,
		  0x00091009, 0x1FFF1FFF, 0x1FFE1FFE, 0x1FFE1FFE, 0x1FFE1FFE, 0x10041008, 0x00040008, 0x80008000 },
		// PIPE
		{ 0x00130017, 0x4FA870F0, 0x4510BCE0, 0x8500BEF0, 0x54C05F80, 0x02AF0371, 0x01DF02E5, 0x01D702B0,
		  0x026A0358, 0x011E01D6, 0x00B1012D, 0x0059011F, 0x00E301A0, 0x00400058, 0x00140028, 0x80008000 },
	};

	XAPO_REGISTRATION_PROPERTIES MakeRegistration()
	{
		XAPO_REGISTRATION_PROPERTIES properties{};
		properties.clsid = {0xe3508fb4, 0x71cb, 0x4573, {0xa5, 0x91, 0x92, 0xc4, 0x36, 0xdf, 0x72, 0x0d}};
		wcscpy_s(properties.FriendlyName, L"ProjectCane SPU2 Studio C Reverb");
		wcscpy_s(properties.CopyrightInfo, L"ProjectCane");
		properties.MajorVersion = 1;
		properties.MinorVersion = 0;
		properties.Flags = XAPO_FLAG_CHANNELS_MUST_MATCH |
			XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
			XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | XAPO_FLAG_INPLACE_SUPPORTED;
		properties.MinInputBufferCount = properties.MaxInputBufferCount = 1;
		properties.MinOutputBufferCount = properties.MaxOutputBufferCount = 1;
		return properties;
	}

	const XAPO_REGISTRATION_PROPERTIES kRegistration = MakeRegistration();

	XAPO_REGISTRATION_PROPERTIES MakeSaturatorRegistration()
	{
		XAPO_REGISTRATION_PROPERTIES properties{};
		properties.clsid = {0x943fa44d, 0xbec9, 0x46c7, {0x87, 0x12, 0x93, 0x3a, 0x15, 0xc6, 0x6d, 0x9e}};
		wcscpy_s(properties.FriendlyName, L"ProjectCane SPU2 Mix Saturator");
		wcscpy_s(properties.CopyrightInfo, L"ProjectCane");
		properties.MajorVersion = 1;
		properties.MinorVersion = 0;
		properties.Flags = XAPO_FLAG_CHANNELS_MUST_MATCH |
			XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
			XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | XAPO_FLAG_INPLACE_SUPPORTED;
		properties.MinInputBufferCount = properties.MaxInputBufferCount = 1;
		properties.MinOutputBufferCount = properties.MaxOutputBufferCount = 1;
		return properties;
	}

	const XAPO_REGISTRATION_PROPERTIES kSaturatorRegistration = MakeSaturatorRegistration();
	constexpr GUID kIeeeFloatSubFormat =
		{WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010,
			{0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

	bool IsStereoFloat48k(const WAVEFORMATEX* format)
	{
		if (!format || format->nChannels != 2 || format->nSamplesPerSec != 48000 ||
			format->wBitsPerSample != 32)
		{
			return false;
		}

		if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
			return true;

		if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
			format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
		{
			return false;
		}

		const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
		return extensible->Samples.wValidBitsPerSample == 32 &&
			IsEqualGUID(extensible->SubFormat, kIeeeFloatSubFormat);
	}

	void AtomicMax(std::atomic<float>& destination, const float value)
	{
		float current = destination.load(std::memory_order_relaxed);
		while (current < value && !destination.compare_exchange_weak(current, value,
			std::memory_order_relaxed, std::memory_order_relaxed))
		{
		}
	}
}

HRESULT SpuMixSaturator::QueryInterface(const REFIID interfaceId, void** result)
{
	if (!result)
		return E_POINTER;
	if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IXAPO))
	{
		*result = static_cast<IXAPO*>(this);
		AddRef();
		return S_OK;
	}
	*result = nullptr;
	return E_NOINTERFACE;
}

ULONG SpuMixSaturator::AddRef() { return ++m_references; }

void SpuMixSaturator::SetTargetGain(const float gain)
{
	m_targetGain.store(std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 0.0f,
		std::memory_order_release);
}

void SpuMixSaturator::BeginDiagnosticCapture()
{
	if (!m_finalOutput)
		return;
	m_diagnosticCaptureEnabled.store(false, std::memory_order_release);
	m_diagnosticSampleCount.store(0, std::memory_order_release);
	m_diagnosticInvalidInputSamples.store(0, std::memory_order_release);
	m_diagnosticClippedInputSamples.store(0, std::memory_order_release);
	m_diagnosticClippedOutputSamples.store(0, std::memory_order_release);
	m_diagnosticInputPeak.store(0.0f, std::memory_order_release);
	m_diagnosticOutputPeak.store(0.0f, std::memory_order_release);
	m_diagnosticInputMaxJump.store(0.0f, std::memory_order_release);
	m_diagnosticOutputMaxJump.store(0.0f, std::memory_order_release);
	m_diagnosticPreviousInput = {};
	m_diagnosticPreviousOutput = {};
	m_diagnosticCaptureEnabled.store(true, std::memory_order_release);
}

SpuMixSaturator::DiagnosticStats SpuMixSaturator::GetDiagnosticStats() const
{
	DiagnosticStats stats;
	stats.samples = m_diagnosticSampleCount.load(std::memory_order_acquire);
	stats.invalidInputSamples = m_diagnosticInvalidInputSamples.load(std::memory_order_acquire);
	stats.clippedInputSamples = m_diagnosticClippedInputSamples.load(std::memory_order_acquire);
	stats.clippedOutputSamples = m_diagnosticClippedOutputSamples.load(std::memory_order_acquire);
	stats.inputPeak = m_diagnosticInputPeak.load(std::memory_order_acquire);
	stats.outputPeak = m_diagnosticOutputPeak.load(std::memory_order_acquire);
	stats.inputMaxJump = m_diagnosticInputMaxJump.load(std::memory_order_acquire);
	stats.outputMaxJump = m_diagnosticOutputMaxJump.load(std::memory_order_acquire);
	return stats;
}

SpuMixSaturator::DiagnosticCapture SpuMixSaturator::EndDiagnosticCapture()
{
	DiagnosticCapture capture;
	if (!m_finalOutput)
		return capture;
	m_diagnosticCaptureEnabled.store(false, std::memory_order_release);
	const std::size_t count = (std::min)(m_diagnosticSampleCount.load(std::memory_order_acquire),
		kDiagnosticSampleCapacity);
	capture.input.assign(m_diagnosticInput.begin(), m_diagnosticInput.begin() + count);
	capture.output.assign(m_diagnosticOutput.begin(), m_diagnosticOutput.begin() + count);
	return capture;
}

ULONG SpuMixSaturator::Release()
{
	const ULONG references = --m_references;
	if (references == 0)
		delete this;
	return references;
}

HRESULT SpuMixSaturator::GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** properties)
{
	if (!properties)
		return E_POINTER;
	auto* copy = static_cast<XAPO_REGISTRATION_PROPERTIES*>(XAPOAlloc(sizeof(kSaturatorRegistration)));
	if (!copy)
		return E_OUTOFMEMORY;
	*copy = kSaturatorRegistration;
	*properties = copy;
	return S_OK;
}

HRESULT SpuMixSaturator::IsInputFormatSupported(const WAVEFORMATEX* outputFormat,
	const WAVEFORMATEX* requestedInputFormat, WAVEFORMATEX** supportedInputFormat)
{
	if (supportedInputFormat)
		*supportedInputFormat = nullptr;
	return IsStereoFloat48k(outputFormat) && IsStereoFloat48k(requestedInputFormat)
		? S_OK : XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT SpuMixSaturator::IsOutputFormatSupported(const WAVEFORMATEX* inputFormat,
	const WAVEFORMATEX* requestedOutputFormat, WAVEFORMATEX** supportedOutputFormat)
{
	if (supportedOutputFormat)
		*supportedOutputFormat = nullptr;
	return IsStereoFloat48k(inputFormat) && IsStereoFloat48k(requestedOutputFormat)
		? S_OK : XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT SpuMixSaturator::Initialize(const void*, const UINT32) { return S_OK; }
void SpuMixSaturator::Reset()
{
	m_previousInput = {};
	m_previousOutput = {};
	m_currentGain = m_targetGain.load(std::memory_order_acquire);
}

HRESULT SpuMixSaturator::LockForProcess(const UINT32 inputCount,
	const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* inputs, const UINT32 outputCount,
	const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* outputs)
{
	if (m_locked || inputCount != 1 || outputCount != 1 || !inputs || !outputs)
		return E_INVALIDARG;
	if (!IsStereoFloat48k(inputs[0].pFormat) || !IsStereoFloat48k(outputs[0].pFormat))
		return XAPO_E_FORMAT_UNSUPPORTED;
	m_locked = true;
	return S_OK;
}

void SpuMixSaturator::UnlockForProcess() { m_locked = false; }
UINT32 SpuMixSaturator::CalcInputFrames(const UINT32 outputFrames) { return outputFrames; }
UINT32 SpuMixSaturator::CalcOutputFrames(const UINT32 inputFrames) { return inputFrames; }

void SpuMixSaturator::Process(UINT32 inputCount,
	const XAPO_PROCESS_BUFFER_PARAMETERS* inputs, UINT32 outputCount,
	XAPO_PROCESS_BUFFER_PARAMETERS* outputs, const BOOL enabled)
{
	if (inputCount != 1 || outputCount != 1)
		return;
	const UINT32 frames = inputs[0].ValidFrameCount;
	auto* input = static_cast<const float*>(inputs[0].pBuffer);
	auto* output = static_cast<float*>(outputs[0].pBuffer);
	outputs[0].ValidFrameCount = frames;
	outputs[0].BufferFlags = inputs[0].BufferFlags;
	const bool silent = inputs[0].BufferFlags == XAPO_BUFFER_SILENT;
	constexpr float kSpuPositiveMaximum = 32767.0f / 32768.0f;
	if (!m_finalOutput)
	{
		for (UINT32 sample = 0; sample < frames * 2; ++sample)
		{
			const float value = silent ? 0.0f :
				(std::isfinite(input[sample]) ? input[sample] : 0.0f);
			output[sample] = enabled ? std::clamp(value, -1.0f, kSpuPositiveMaximum) : value;
		}
	}
	else
	{
		outputs[0].BufferFlags = XAPO_BUFFER_VALID;
		constexpr float kGainStep = 1.0f / (48000.0f * 0.015f);
		constexpr float kDcPole = 0.995f;
		const bool captureDiagnostics =
			m_diagnosticCaptureEnabled.load(std::memory_order_acquire);
		const std::size_t requestedSamples = static_cast<std::size_t>(frames) * 2;
		const std::size_t diagnosticBegin = captureDiagnostics
			? m_diagnosticSampleCount.fetch_add(requestedSamples, std::memory_order_acq_rel) : 0;
		const std::size_t diagnosticCount = captureDiagnostics &&
			diagnosticBegin < kDiagnosticSampleCapacity
			? (std::min)(requestedSamples, kDiagnosticSampleCapacity - diagnosticBegin) : 0;
		for (UINT32 frame = 0; frame < frames; ++frame)
		{
			const float target = m_targetGain.load(std::memory_order_acquire);
			if (m_currentGain < target)
				m_currentGain = (std::min)(target, m_currentGain + kGainStep);
			else if (m_currentGain > target)
				m_currentGain = (std::max)(target, m_currentGain - kGainStep);
			for (UINT32 channel = 0; channel < 2; ++channel)
			{
				const UINT32 sample = frame * 2 + channel;
				const float rawSource = silent ? 0.0f : input[sample];
				if (captureDiagnostics)
				{
					if (!std::isfinite(rawSource))
						m_diagnosticInvalidInputSamples.fetch_add(1, std::memory_order_relaxed);
					else if (std::abs(rawSource) > 1.0f)
						m_diagnosticClippedInputSamples.fetch_add(1, std::memory_order_relaxed);
				}
				const float source = silent ? 0.0f :
					(std::isfinite(input[sample]) ? input[sample] : 0.0f);
				if (sample < diagnosticCount)
					m_diagnosticInput[diagnosticBegin + sample] = source;
				const float filtered = source - m_previousInput[channel] +
					kDcPole * m_previousOutput[channel];
				m_previousInput[channel] = source;
				m_previousOutput[channel] = std::isfinite(filtered) ? filtered : 0.0f;
				const float gained = m_previousOutput[channel] * m_currentGain;
				output[sample] = enabled ? std::clamp(gained, -1.0f, kSpuPositiveMaximum) : gained;
				if (captureDiagnostics)
				{
					const float inputMagnitude = std::abs(source);
					const float outputMagnitude = std::abs(output[sample]);
					AtomicMax(m_diagnosticInputPeak, inputMagnitude);
					AtomicMax(m_diagnosticOutputPeak, outputMagnitude);
					AtomicMax(m_diagnosticInputMaxJump,
						std::abs(source - m_diagnosticPreviousInput[channel]));
					AtomicMax(m_diagnosticOutputMaxJump,
						std::abs(output[sample] - m_diagnosticPreviousOutput[channel]));
					m_diagnosticPreviousInput[channel] = source;
					m_diagnosticPreviousOutput[channel] = output[sample];
					if (std::abs(gained) > 1.0f)
						m_diagnosticClippedOutputSamples.fetch_add(1, std::memory_order_relaxed);
				}
				if (sample < diagnosticCount)
					m_diagnosticOutput[diagnosticBegin + sample] = output[sample];
			}
		}
	}
}

SpuStudioCReverb::SpuStudioCReverb()
	: m_effectBuffer(kMaxEffectBufferSize, 0)
{
	LoadModeLocked(4);
}

void SpuStudioCReverb::LoadModeLocked(const int sdMode)
{
	const int mode = (sdMode >= 0 && sdMode < static_cast<int>(kEffectSizes.size())) ? sdMode : 0;
	uint16_t d[32];
	for (int i = 0; i < 16; ++i)
	{
		d[i * 2] = static_cast<uint16_t>(kEffectParams[mode][i] & 0xFFFF);
		d[i * 2 + 1] = static_cast<uint16_t>(kEffectParams[mode][i] >> 16);
	}
	// Address registers are written as value * 4 samples (SetEffectRegister:
	// value << 2), volumes are signed Q15. Index [0] = left, [1] = right.
	const auto a = [&](int i) { return static_cast<int>(d[i]) * 4; };
	const auto v = [&](int i) { return static_cast<int>(static_cast<int16_t>(d[i])); };
	Params p{};
	p.apf1Size = a(0);  p.apf2Size = a(1);
	p.vIir = v(2); p.vComb1 = v(3); p.vComb2 = v(4); p.vComb3 = v(5);
	p.vComb4 = v(6); p.vWall = v(7); p.vApf1 = v(8); p.vApf2 = v(9);
	p.sameDst[0] = a(10); p.sameDst[1] = a(11);   // IIR_DEST_A0/A1
	p.comb1[0] = a(12);   p.comb1[1] = a(13);     // ACC_SRC_A0/A1
	p.comb2[0] = a(14);   p.comb2[1] = a(15);     // ACC_SRC_B0/B1
	p.sameSrc[0] = a(16); p.sameSrc[1] = a(17);   // IIR_SRC_A0/A1
	p.diffDst[0] = a(18); p.diffDst[1] = a(19);   // IIR_DEST_B0/B1
	p.comb3[0] = a(20);   p.comb3[1] = a(21);     // ACC_SRC_C0/C1
	p.comb4[0] = a(22);   p.comb4[1] = a(23);     // ACC_SRC_D0/D1
	// The hardware register order is IIR_SRC_B1 then IIR_SRC_B0, and each side
	// reads the opposite side's diffusion source (as in PCSX2's DoReverb).
	p.diffSrc[0] = a(24); p.diffSrc[1] = a(25);
	p.apf1Dst[0] = a(26); p.apf1Dst[1] = a(27);   // MIX_DEST_A0/A1
	p.apf2Dst[0] = a(28); p.apf2Dst[1] = a(29);   // MIX_DEST_B0/B1
	p.vIn[0] = v(30);     p.vIn[1] = v(31);
	m_params = p;
	m_mode = mode;
	m_effectBufferSize = (std::max)(1, static_cast<int>(kEffectSizes[mode]) * 8);
}

void SpuStudioCReverb::SetMode(const int sdMode)
{
	{
		const std::lock_guard<std::mutex> lock(m_processMutex);
		LoadModeLocked(sdMode);
	}
	Reset();
}

void SpuStudioCReverb::SetActive(const bool active)
{
	if (active)
	{
		if (m_active.load(std::memory_order_acquire))
			return;
		// Clear every delay/filter element before exposing the processor. Keeping
		// the gate closed while Reset runs prevents the XAudio thread from seeing
		// a partially reset Studio C work area.
		Reset();
		m_active.store(true, std::memory_order_release);
		return;
	}

	if (!m_active.exchange(false, std::memory_order_acq_rel))
		return;
	// Close the gate first, then discard the old environment's feedback tail.
	Reset();
}

HRESULT SpuStudioCReverb::QueryInterface(const REFIID interfaceId, void** result)
{
	if (!result)
		return E_POINTER;
	if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IXAPO))
	{
		*result = static_cast<IXAPO*>(this);
		AddRef();
		return S_OK;
	}
	*result = nullptr;
	return E_NOINTERFACE;
}

ULONG SpuStudioCReverb::AddRef()
{
	return ++m_references;
}

ULONG SpuStudioCReverb::Release()
{
	const ULONG references = --m_references;
	if (references == 0)
		delete this;
	return references;
}

HRESULT SpuStudioCReverb::GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** properties)
{
	if (!properties)
		return E_POINTER;
	auto* copy = static_cast<XAPO_REGISTRATION_PROPERTIES*>(XAPOAlloc(sizeof(kRegistration)));
	if (!copy)
		return E_OUTOFMEMORY;
	*copy = kRegistration;
	*properties = copy;
	return S_OK;
}

HRESULT SpuStudioCReverb::IsInputFormatSupported(const WAVEFORMATEX* outputFormat,
	const WAVEFORMATEX* requestedInputFormat, WAVEFORMATEX** supportedInputFormat)
{
	if (supportedInputFormat)
		*supportedInputFormat = nullptr;
	return IsStereoFloat48k(outputFormat) && IsStereoFloat48k(requestedInputFormat)
		? S_OK : XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT SpuStudioCReverb::IsOutputFormatSupported(const WAVEFORMATEX* inputFormat,
	const WAVEFORMATEX* requestedOutputFormat, WAVEFORMATEX** supportedOutputFormat)
{
	if (supportedOutputFormat)
		*supportedOutputFormat = nullptr;
	return IsStereoFloat48k(inputFormat) && IsStereoFloat48k(requestedOutputFormat)
		? S_OK : XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT SpuStudioCReverb::Initialize(const void*, const UINT32)
{
	return S_OK;
}

HRESULT SpuStudioCReverb::LockForProcess(const UINT32 inputCount,
	const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* inputs, const UINT32 outputCount,
	const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* outputs)
{
	if (m_locked || inputCount != 1 || outputCount != 1 || !inputs || !outputs)
		return E_INVALIDARG;
	if (!IsStereoFloat48k(inputs[0].pFormat) || !IsStereoFloat48k(outputs[0].pFormat))
		return XAPO_E_FORMAT_UNSUPPORTED;
	m_locked = true;
	Reset();
	return S_OK;
}

void SpuStudioCReverb::UnlockForProcess()
{
	m_locked = false;
}

UINT32 SpuStudioCReverb::CalcInputFrames(const UINT32 outputFrames)
{
	return outputFrames;
}

UINT32 SpuStudioCReverb::CalcOutputFrames(const UINT32 inputFrames)
{
	return inputFrames;
}

void SpuStudioCReverb::Reset()
{
	const std::lock_guard<std::mutex> lock(m_processMutex);
	ClearStateUnlocked();
}

void SpuStudioCReverb::ClearStateUnlocked()
{
	std::fill(m_effectBuffer.begin(), m_effectBuffer.end(), 0);
	m_down = {};
	m_up = {};
	m_ringPosition = 0;
	m_cycle = 0;
}

bool SpuStudioCReverb::HasStateUnlocked() const
{
	return std::any_of(m_effectBuffer.begin(), m_effectBuffer.end(),
		[](const short sample) { return sample != 0; }) ||
		std::any_of(m_down.begin(), m_down.end(), [](const auto& channel)
		{
			return std::any_of(channel.begin(), channel.end(),
				[](const short sample) { return sample != 0; });
		}) ||
		std::any_of(m_up.begin(), m_up.end(), [](const auto& channel)
		{
			return std::any_of(channel.begin(), channel.end(),
				[](const short sample) { return sample != 0; });
		});
}

int SpuStudioCReverb::Clamp16(const int64_t value)
{
	return static_cast<int>(std::clamp<int64_t>(value, -32768, 32767));
}

int64_t SpuStudioCReverb::MulQ15(const int64_t left, const int64_t right)
{
	return (left * right) >> 15;
}

int SpuStudioCReverb::EffectIndex(const int offset) const
{
	const long long value = static_cast<long long>(m_cycle >> 1) + offset;
	const long long wrapped = value % m_effectBufferSize;
	return static_cast<int>(wrapped < 0 ? wrapped + m_effectBufferSize : wrapped);
}

int SpuStudioCReverb::Downsample(const bool right) const
{
	const int index = (static_cast<int>(m_ringPosition) - kTapCount) & 63;
	int64_t value = 0;
	for (int tap = 0; tap < kTapCount; ++tap)
		value += m_down[right ? 1 : 0][index + tap] * kDownFilter[tap];
	return Clamp16(value >> 15);
}

std::array<int, 2> SpuStudioCReverb::Upsample() const
{
	const int index = (static_cast<int>(m_ringPosition) - kTapCount) & 63;
	std::array<int, 2> result{};
	for (int channel = 0; channel < 2; ++channel)
	{
		int64_t value = 0;
		for (int tap = 0; tap < kTapCount; ++tap)
		{
			const int coefficient = Clamp16(kDownFilter[tap] * 2);
			value += m_up[channel][index + tap] * coefficient;
		}
		result[channel] = Clamp16(value >> 15);
	}
	return result;
}

std::array<int, 2> SpuStudioCReverb::ProcessFrame(const int left, const int right)
{
	const short inputLeft = static_cast<short>(Clamp16(left));
	const short inputRight = static_cast<short>(Clamp16(right));
	m_down[0][m_ringPosition] = m_down[0][m_ringPosition | 64] = inputLeft;
	m_down[1][m_ringPosition] = m_down[1][m_ringPosition | 64] = inputRight;

	const bool processRight = (m_cycle & 1) != 0;
	const int side = processRight ? 1 : 0;
	const Params& p = m_params;
	const int sameSource = EffectIndex(p.sameSrc[side]);
	const int sameDestination = EffectIndex(p.sameDst[side]);
	const int samePrevious = EffectIndex(p.sameDst[side] - 1);
	const int diffSource = EffectIndex(p.diffSrc[side]);
	const int diffDestination = EffectIndex(p.diffDst[side]);
	const int diffPrevious = EffectIndex(p.diffDst[side] - 1);
	const int comb1Source = EffectIndex(p.comb1[side]);
	const int comb2Source = EffectIndex(p.comb2[side]);
	const int comb3Source = EffectIndex(p.comb3[side]);
	const int comb4Source = EffectIndex(p.comb4[side]);
	const int apf1Source = EffectIndex(p.apf1Dst[side] - p.apf1Size);
	const int apf1Destination = EffectIndex(p.apf1Dst[side]);
	const int apf2Source = EffectIndex(p.apf2Dst[side] - p.apf2Size);
	const int apf2Destination = EffectIndex(p.apf2Dst[side]);

	const int64_t filteredInput = MulQ15(p.vIn[side], Downsample(processRight));
	const int64_t same = MulQ15(p.vIir, filteredInput + MulQ15(p.vWall, m_effectBuffer[sameSource]) -
		m_effectBuffer[samePrevious]) + m_effectBuffer[samePrevious];
	const int64_t diff = MulQ15(p.vIir, filteredInput + MulQ15(p.vWall, m_effectBuffer[diffSource]) -
		m_effectBuffer[diffPrevious]) + m_effectBuffer[diffPrevious];
	int64_t output = MulQ15(p.vComb1, m_effectBuffer[comb1Source]) +
		MulQ15(p.vComb2, m_effectBuffer[comb2Source]) +
		MulQ15(p.vComb3, m_effectBuffer[comb3Source]) +
		MulQ15(p.vComb4, m_effectBuffer[comb4Source]);
	const int64_t apf1 = output - MulQ15(p.vApf1, m_effectBuffer[apf1Source]);
	output = m_effectBuffer[apf1Source] + MulQ15(p.vApf1, apf1);
	const int64_t apf2 = output - MulQ15(p.vApf2, m_effectBuffer[apf2Source]);
	output = m_effectBuffer[apf2Source] + MulQ15(p.vApf2, apf2);

	m_effectBuffer[sameDestination] = static_cast<short>(Clamp16(same));
	m_effectBuffer[diffDestination] = static_cast<short>(Clamp16(diff));
	m_effectBuffer[apf1Destination] = static_cast<short>(Clamp16(apf1));
	m_effectBuffer[apf2Destination] = static_cast<short>(Clamp16(apf2));
	output = Clamp16(output);
	m_up[processRight ? 1 : 0][m_ringPosition] = static_cast<short>(output);
	m_up[processRight ? 0 : 1][m_ringPosition] = 0;
	m_up[processRight ? 1 : 0][m_ringPosition | 64] = static_cast<short>(output);
	m_up[processRight ? 0 : 1][m_ringPosition | 64] = 0;
	m_ringPosition = (m_ringPosition + 1) & 63;
	++m_cycle;
	return Upsample();
}

void SpuStudioCReverb::Process(UINT32 inputCount,
	const XAPO_PROCESS_BUFFER_PARAMETERS* inputs, UINT32 outputCount,
	XAPO_PROCESS_BUFFER_PARAMETERS* outputs, const BOOL enabled)
{
	if (inputCount != 1 || outputCount != 1)
		return;
	const std::lock_guard<std::mutex> lock(m_processMutex);
	const UINT32 frames = inputs[0].ValidFrameCount;
	auto* input = static_cast<const float*>(inputs[0].pBuffer);
	auto* output = static_cast<float*>(outputs[0].pBuffer);
	outputs[0].ValidFrameCount = frames;
	if (!m_active.load(std::memory_order_acquire))
	{
		// A zero output matrix is a routing control, not a guarantee that XAudio
		// will label the effect input SILENT. Never let an inactive feedback XAPO
		// turn a stale VALID work buffer into an audible reverb tail.
		std::fill_n(output, static_cast<std::size_t>(frames) * 2, 0.0f);
		outputs[0].BufferFlags = XAPO_BUFFER_SILENT;
		return;
	}
	if (!enabled)
	{
		// This processor is a wet-only return. XAPO bypass semantics would copy
		// the dry input into that return and double the source signal, so an
		// explicitly disabled Studio C must instead be silent. Discard its delay
		// state as well so a later enable cannot resurrect an old environment.
		std::fill_n(output, static_cast<std::size_t>(frames) * 2, 0.0f);
		ClearStateUnlocked();
		outputs[0].BufferFlags = XAPO_BUFFER_SILENT;
		return;
	}
	outputs[0].BufferFlags = XAPO_BUFFER_VALID;
	bool producedOutput = false;
	for (UINT32 frame = 0; frame < frames; ++frame)
	{
		float left = inputs[0].BufferFlags == XAPO_BUFFER_SILENT ? 0.0f : input[frame * 2];
		float right = inputs[0].BufferFlags == XAPO_BUFFER_SILENT ? 0.0f : input[frame * 2 + 1];
		// Reject malformed first-buffer samples before converting them to the
		// SPU's fixed-point domain. lround(NaN/Inf) is not a safe conversion and
		// could otherwise turn a rare device-start transient into full-scale noise.
		left = std::isfinite(left) ? std::clamp(left, -1.0f, 1.0f) : 0.0f;
		right = std::isfinite(right) ? std::clamp(right, -1.0f, 1.0f) : 0.0f;
		const auto wet = ProcessFrame(static_cast<int>(std::lround(left * 32768.0f)),
			static_cast<int>(std::lround(right * 32768.0f)));
		output[frame * 2] = std::clamp(wet[0] / 32768.0f, -1.0f, 1.0f);
		output[frame * 2 + 1] = std::clamp(wet[1] / 32768.0f, -1.0f, 1.0f);
		producedOutput = producedOutput || wet[0] != 0 || wet[1] != 0;
	}

	// Once both the output and every fixed-point delay/filter element are empty,
	// propagate SILENT so XAudio can stop scheduling an exhausted reverb tail.
	if (inputs[0].BufferFlags == XAPO_BUFFER_SILENT && !producedOutput &&
		!HasStateUnlocked())
	{
		outputs[0].BufferFlags = XAPO_BUFFER_SILENT;
	}
}
