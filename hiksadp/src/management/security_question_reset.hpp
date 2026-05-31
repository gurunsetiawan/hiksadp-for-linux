#pragma once

#include "management/isapi_client.hpp"

namespace hiksadp {

enum class SecurityQuestionResetDecision {
    Success,
    ContinueFallback,
    AuthenticationFailed,
    UnexpectedFailure,
};

[[nodiscard]] SecurityQuestionResetDecision
classify_security_question_reset_response(const IsapiResponse& resp);

} // namespace hiksadp
