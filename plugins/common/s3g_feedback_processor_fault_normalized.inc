void applyNormalizedParam(FaultReadout& p, clap_id id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    switch (id) {
    case kBitDepthParamId: applyParam(p, id, std::round(2.0 + normalized * 14.0)); break;
    case kCodecModeParamId: applyParam(p, id, std::round(normalized * static_cast<double>(kCodecModeMax))); break;
    case kChannelSchemeParamId: applyParam(p, id, std::round(normalized * 4.0)); break;
    case kGainParamId: applyParam(p, id, -60.0 + normalized * 66.0); break;
    case kCarrierTuneParamId: applyParam(p, id, -24.0 + normalized * 48.0); break;
    case kModSourceParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModTargetParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModRatioParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedbackParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLockParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModAlgorithmParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u))); break;
    case kModSource2ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModRatio2ParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedback2ParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLock2ParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModTarget2ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModSource3ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModTarget3ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModRatio3ParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedback3ParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLock3ParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModEnvelope1ParamId:
    case kModEnvelope2ParamId:
    case kModEnvelope3ParamId:
    case kModulationEnabledParamId:
        applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0);
        break;
    case kBassReceiverParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u))); break;
    case kBassPitchTrackingParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u))); break;
    case kBassOctaveParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u))); break;
    case kBassBodyParamId:
    case kBassPunchParamId:
    case kBassTraceParamId:
    case kBassGlideParamId:
    case kBassLowWidthParamId:
    case kBassFuzzParamId:
    case kBassMetalParamId:
    case kBassFeedbackParamId:
        applyParam(p, id, normalized);
        break;
    case kSeedParamId: applyParam(p, id, 1.0 + normalized * 4294967294.0); break;
    case kAttackParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 1.0, 5000.0)); break;
    case kDecayParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 8000.0)); break;
    case kSustainParamId: applyParam(p, id, normalized); break;
    case kReleaseParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 12000.0)); break;
    case kOutputRotationParamId: applyParam(p, id, -180.0 + normalized * 360.0); break;
    default: applyParam(p, id, normalized); break;
    }
}
