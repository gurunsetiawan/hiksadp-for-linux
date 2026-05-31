#include "management/security_question_reset.hpp"

#include <algorithm>
#include <cctype>

namespace hiksadp {

namespace {
std::string lower_copy(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool body_indicates_wrong_answer(const std::string& body)
{
    const auto b = lower_copy(body);
    return b.find("wrong answer") != std::string::npos ||
           b.find("invalid answer") != std::string::npos ||
           b.find("answer error") != std::string::npos ||
           b.find("securityanswererror") != std::string::npos;
}
} // namespace

SecurityQuestionResetDecision
classify_security_question_reset_response(const IsapiResponse& resp)
{
    if (resp.is_ok()) return SecurityQuestionResetDecision::Success;

    if (resp.status_code == 400) {
        return body_indicates_wrong_answer(resp.body)
            ? SecurityQuestionResetDecision::AuthenticationFailed
            : SecurityQuestionResetDecision::ContinueFallback;
    }

    if (resp.status_code == 401 || resp.status_code == 403) {
        return SecurityQuestionResetDecision::AuthenticationFailed;
    }

    if (resp.status_code == 404 ||
        resp.status_code == 405 ||
        resp.status_code == 501) {
        return SecurityQuestionResetDecision::ContinueFallback;
    }

    return SecurityQuestionResetDecision::UnexpectedFailure;
}

} // namespace hiksadp
