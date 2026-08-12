#define BOOST_TEST_MODULE TexSolveParser
#include <boost/test/included/unit_test.hpp>

#include <string_view>

#include "internal.hpp"

BOOST_AUTO_TEST_CASE(accepts_documented_whitelist_examples) {
    constexpr std::string_view inputs[] = {
        "12", "3.5", "2.1e-4", "x", R"(\alpha)", R"(\pi)",
        R"(\operatorname{rate})", R"(\frac{1}{x+1})", R"(\sqrt{x})",
        R"(\sqrt[3]{8})", "x^{2}", "5!", "2x", "x(y+1)",
        R"(\sin{x}+\ln{2})", R"(\log_{2}{8})", R"(\left|x\right|)",
        R"(\abs{x})", R"(\max(1,2,x))", "f(x,y):=x+y", "f(1,2)",
        R"(\frac{d}{dx}{x^2})", R"(\frac{d^2}{dx^2}{x^3})",
        R"(\frac{\partial}{\partial x}{xy})", R"(\int x^2\,dx)",
        R"(\int_{0}^{1}x^2\,dx)", R"(\iint_{0}^{1}xy\,dx\,dy)",
        R"(\lim_{x\to 0}\frac{\sin{x}}{x})", R"(\sum_{k=1}^{10}k)",
        R"(\prod_{k=1}^{5}k)",
        R"(\begin{pmatrix}1&2\\3&4\end{pmatrix})",
        R"(\det\begin{pmatrix}1&2\\3&4\end{pmatrix})", "A^{T}", "x^2=4",
        R"(\begin{cases}x+y=2\\x-y=0\end{cases})", "x^2\\le 4",
        R"(\min_{x,y}{(x-1)^2+y^2})", R"(\min_{x}{x^2},\;x\ge 1)",
        R"(\frac{dy}{dt}=y,\;y(0)=1,\;t\in[0,1])",
        R"(\frac{d^2y}{dt^2}=-y,\;y(0)=0,\;y'(0)=1,\;t\in[0,1])",
        R"(\begin{bmatrix}1&0\\0&1\end{bmatrix})"};
    for (const auto input : inputs) {
        const auto parsed = texsolve::parse_for_debug(input, 128, 50000);
        BOOST_TEST_CONTEXT(input) {
            BOOST_TEST_INFO(parsed.message);
            BOOST_TEST(parsed.ok);
        }
    }
}

BOOST_AUTO_TEST_CASE(rejects_documented_invalid_examples) {
    constexpr std::string_view inputs[] = {
        ".", "1e", "01.2.3", R"(\Pi)", R"(\unknown)", R"(\frac{1})",
        R"(\sqrt[0]{x})", "x^", "(-1.2)!", "2 3", "f x", R"(\Sin{x})",
        R"(\log_{1}{8})", "|x", R"(\max())", "f(x,x):=x", "f()",
        R"(\frac{d^2}{dx}{x})", R"(\frac{\partial}{dx}{xy})", R"(\int x^2)",
        R"(\int_{0}x\,dx)", R"(\iiint xyz\,dx\,dy\,dz)", R"(\lim_{x=0}x)",
        R"(\sum_{k=1}^{\infty}k)", R"(\prod_{1}^{5}k)",
        R"(\begin{pmatrix}1&2\\3\end{pmatrix})", R"(\det{x})", "2^{T}",
        "x^2==4", R"(\begin{cases}x+y\end{cases})", "x<>2", R"(\min{})",
        R"(\min_{x}{x^2},\;x)", R"(\frac{dy}{dt}=y)", R"(\newcommand{x}{1})",
        R"(\frac{d^2y}{dt^2}=-y,\;y(0)=0,\;t\in[0,1])",
        "x+1 trailing"};
    for (const auto input : inputs) {
        const auto parsed = texsolve::parse_for_debug(input, 128, 50000);
        BOOST_TEST_CONTEXT(input) {
            BOOST_TEST_INFO(parsed.ast);
            BOOST_TEST(!parsed.ok);
        }
    }
}

BOOST_AUTO_TEST_CASE(emits_deterministic_spans_and_honors_limits) {
    const auto parsed = texsolve::parse_for_debug("1+x/2", 128, 50000);
    BOOST_REQUIRE(parsed.ok);
    BOOST_TEST(parsed.ast.find("Binary [0,5) +") != std::string::npos);
    BOOST_TEST(!texsolve::parse_for_debug("((((x))))", 2, 50000).ok);
    BOOST_TEST(!texsolve::parse_for_debug("1+2+3", 128, 3).ok);
    BOOST_TEST(!texsolve::parse_for_debug(std::string(200, '-') + "1", 32, 50000).ok);
    BOOST_TEST(!texsolve::parse_for_debug(R"(\begin{cases}x=1\\y=2\end{cases})", 128, 5).ok);
    BOOST_TEST(!texsolve::parse_for_debug(R"((\int (x)\,dx))", 1, 50000).ok);
    BOOST_TEST(!texsolve::parse_for_debug(R"((\begin{cases}(x)=1\end{cases}))", 1, 50000).ok);
}
