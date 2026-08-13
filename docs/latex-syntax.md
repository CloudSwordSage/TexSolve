# TexSolve LaTeX 输入子集

## 1. 词法约定

输入必须是 UTF-8 数学内容，不包含 `$`、`\(`、`\)` 等文档定界符。空白可出现在 token 之间；命令名区分大小写。标识符是单个拉丁/希腊符号或由 `[A-Za-z_][A-Za-z0-9_]*` 组成的 `\operatorname{name}`。内建常量为 `\pi`、`e`、`i`、`\infty`。

不执行 TeX 宏展开。`\newcommand`、`\def`、`\input`、`\include`、`\usepackage` 和任何未列出的反斜杠命令都在词法阶段拒绝。

## 2. EBNF

```ebnf
request       = definition | optimization | ode_ivp | relation_set | expression ;
definition    = symbol ":=" expression
              | function_head ":=" expression ;
function_head = symbol "(" symbol { "," symbol } ")" ;
expression    = sum ;
sum           = product { ("+" | "-") product } ;
product       = prefix { ("*" | "\\cdot" | "\\times" | "/") prefix | implicit prefix } ;
prefix        = ("+" | "-") prefix | power ;
power         = postfix [ "^" prefix ] ;
postfix       = primary { "!" } ;
primary       = derivative | number | constant | function | symbol | fraction | root
              | absolute | group | matrix | integral
              | limit | finite_sum | finite_product ;
group         = "{" expression "}" | "(" expression ")"
              | "\\left(" expression "\\right)" ;
fraction      = "\\frac" group group ;
root          = "\\sqrt" [ "[" integer "]" ] group ;
function      = simple_builtin group
              | "\\log" [ "_" group ] group
              | variadic_builtin "(" arguments ")"
              | matrix_builtin (group | matrix)
              | symbol "(" [ arguments ] ")"
              | operator_name "(" [ arguments ] ")" ;
simple_builtin= "\\sin" | "\\cos" | "\\tan" | "\\arcsin" | "\\arccos"
              | "\\arctan" | "\\sinh" | "\\cosh" | "\\tanh" | "\\exp"
              | "\\ln" | "\\abs" | "\\floor" | "\\ceil" ;
variadic_builtin = "\\min" | "\\max" ;
matrix_builtin = "\\det" | "\\operatorname{rank}" | "\\operatorname{inv}"
               | "\\operatorname{eigenvalues}" | "\\operatorname{eigenvectors}" ;
operator_name = "\\operatorname{" identifier "}" ;
arguments     = expression { "," expression } ;
absolute      = "\\left|" expression "\\right|" ;
relation      = expression ("=" | "<" | ">" | "\\le" | "\\ge") expression ;
relation_set  = relation { "," relation }
              | "\\begin{cases}" relation { "\\\\" relation } "\\end{cases}" ;
matrix        = "\\begin{" matrix_env "}" row { "\\\\" row }
                "\\end{" matrix_env "}" ;
row           = expression { "&" expression } ;
derivative    = "\\frac" "{d" [ "^" integer ] "}" "{d" symbol [ "^" integer ] "}" group
              | "\\frac" "{\\partial" [ "^" integer ] "}"
                "{\\partial" symbol [ "^" integer ] "}" group ;
integral      = "\\int" [ bounds ] expression differential
              | "\\iint" [ bounds ] expression differential differential ;
bounds        = "_" group "^" group ;
differential  = "\\,d" symbol ;
limit         = "\\lim" "_{" symbol "\\to" expression [ side ] "}" expression ;
side          = "^+" | "^-" ;
fold_bounds   = "_{" symbol "=" expression "}" "^{" expression "}" ;
finite_sum    = "\\sum" fold_bounds product ;
finite_product= "\\prod" fold_bounds product ;
optimization  = ("\\min" | "\\max") "_{" symbol { "," symbol } "}" group
                { "," spacing relation } ;
ode_ivp       = ode_clause { "," spacing ode_clause } "," spacing interval_clause
              | "\\begin{cases}" ode_clause { "\\\\" ode_clause }
                "\\end{cases}" "," spacing interval_clause ;
ode_clause    = ode_derivative "=" expression | initial_condition ;
ode_derivative= "\\frac" "{d" symbol "}" "{d" symbol "}"
              | "\\frac" "{d^" integer symbol "}" "{d" symbol "^" integer "}" ;
initial_condition = initial_lhs "=" expression ;
initial_lhs   = symbol { "'" } "(" expression ")" ;
interval_clause = symbol "\\in[" expression "," expression "]" ;
spacing       = [ "\\;" | "\\," ] ;
constant      = "\\pi" | "e" | "i" | "\\infty" ;
symbol        = latin_symbol | greek_command | operator_name ;
number        = integer [ "." digit { digit } ] [ ("e" | "E") [ "+" | "-" ] integer ] ;
integer       = digit { digit } ;
```

`digit` 为 `0` 至 `9`；`latin_symbol` 为单个 `A-Z` 或 `a-z`；`greek_command` 为 `\alpha`、`\beta`、`\gamma`、`\delta`、`\epsilon`、`\theta`、`\lambda`、`\mu`、`\pi`、`\rho`、`\sigma`、`\tau`、`\phi`、`\chi`、`\psi`、`\omega` 及其常见大写形式，其中 `\pi` 在表达式位置解释为常量。`identifier` 匹配 `[A-Za-z_][A-Za-z0-9_]*`。`implicit` 是解析器插入的零宽 token，仅在第 3 节允许的位置出现。`matrix_env` 仅为 `matrix`、`pmatrix`、`bmatrix`。有限求和/乘积的下界必须形如 `k=0`，上下界求值后必须是有限整数。X3 的有序选择必须先尝试具体结构：顶层依次为 definition、optimization、ode_ivp、relation_set、expression，原子层先尝试 derivative 再尝试普通 fraction；顶层解析必须随后匹配输入结尾，避免合法前缀被当作完整请求。

## 3. 优先级与隐式乘法

从低到高依次为关系、加减、乘除/隐式乘法、一元正负、幂、阶乘。幂右结合；一元负号低于幂，因此 `-x^2` 等价于 `-(x^2)`。隐式乘法只允许在无歧义原子之间，例如 `2x`、`3\pi`、`x(y+1)`、`2\sin{x}`；相邻数字、未知命令和两个可形成函数调用的名称必须报歧义错误。

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 数字 | `12`, `3.5`, `2.1e-4` | `.`, `1e`, `01.2.3` |
| 标识符/常量 | `x`, `\alpha`, `\pi`, `\operatorname{rate}` | `\Pi`, `\unknown` |
| 分数 | `\frac{1}{x+1}` | `\frac{1}` |
| 根式 | `\sqrt{x}`, `\sqrt[3]{8}` | `\sqrt[0]{x}` |
| 幂/阶乘 | `x^{2}`, `5!` | `x^`, `(-1.2)!` |
| 隐式乘法 | `2x`, `x(y+1)` | `2 3`, `f x` |

## 4. 函数白名单

内建函数为 `\sin`、`\cos`、`\tan`、`\arcsin`、`\arccos`、`\arctan`、`\sinh`、`\cosh`、`\tanh`、`\exp`、`\ln`、`\log`、`\abs`、`\floor`、`\ceil`、`\min`、`\max`。`\log_{b}{x}` 表示指定底数；无下标的 `\log{x}` 为十进制，`\ln{x}` 为自然对数。

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 初等函数 | `\sin{x}+\ln{2}` | `\Sin{x}` |
| 对数 | `\log_{2}{8}` | `\log_{1}{8}` |
| 绝对值 | `\left|x\right|`, `\abs{x}` | `|x` |
| 多参数函数 | `\max(1,2,x)` | `\max()` |
| 用户函数定义/调用 | `f(x,y):=x+y`, `f(1,2)` | `f(x,x):=x`, `f()`（若定义要求参数） |

## 5. 微积分

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 导数 | `\frac{d}{dx}{x^2}`, `\frac{d^2}{dx^2}{x^3}` | `\frac{d^2}{dx}{x}` |
| 偏导 | `\frac{\partial}{\partial x}{xy}` | `\frac{\partial}{dx}{xy}` |
| 不定积分 | `\int x^2\,dx` | `\int x^2` |
| 定积分 | `\int_{0}^{1}x^2\,dx` | `\int_{0}x\,dx` |
| 多重积分 | `\iint_{0}^{1}xy\,dx\,dy` | `\iiint xyz\,dx\,dy\,dz` |
| 极限 | `\lim_{x\to 0}\frac{\sin{x}}{x}` | `\lim_{x=0}x` |
| 有限求和 | `\sum_{k=1}^{10}k` | `\sum_{k=1}^{\infty}k` |
| 有限乘积 | `\prod_{k=1}^{5}k` | `\prod_{1}^{5}k` |

多重积分首版只支持 `\iint`；每个微分变量必须唯一。微分符号必须写成 `\,dx`，不接受裸 `dx`，避免被隐式乘法解释为 `d x`。数值退化仅适用于边界完整的定积分。有限求和/乘积的主体默认延伸到下一个加减号；主体包含加减时使用圆括号或花括号分组，例如 `\sum_{i=1}^{3}(i+1)`。

## 6. 矩阵与方程

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 矩阵 | `\begin{pmatrix}1&2\\3&4\end{pmatrix}` | `\begin{pmatrix}1&2\\3\end{pmatrix}` |
| 行列式 | `\det\begin{pmatrix}1&2\\3&4\end{pmatrix}` | `\det{x}` |
| 转置 | `A^{T}` | `2^{T}` |
| 方程 | `x^2=4` | `x^2==4` |
| 方程组 | `\begin{cases}x+y=2\\x-y=0\end{cases}` | `\begin{cases}x+y\end{cases}` |
| 不等式 | `x^2\le 4` | `x<>2` |

`\det`、`\operatorname{rank}`、`\operatorname{inv}`、`\operatorname{eigenvalues}` 和 `\operatorname{eigenvectors}` 是矩阵操作白名单。线性求解使用 relation set；不存在赋值语句中的链式等号。

## 7. 优化与 ODE

复杂问题推荐由显式 operation 指定，LaTeX 负责描述数学对象：

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 优化目标 | `\min_{x,y}{(x-1)^2+y^2}` | `\min{}` |
| 约束优化 | `\min_{x}{x^2},\;x\ge 1` | `\min_{x}{x^2},\;x` |
| ODE | `\frac{dy}{dt}=y,\;y(0)=1,\;t\in[0,1]` | `\frac{dy}{dt}=y` |
| 高阶 ODE | `\frac{d^2y}{dt^2}=-y,\;y(0)=0,\;y'(0)=1,\;t\in[0,1]` | `\frac{d^2y}{dt^2}=-y,\;y(0)=0,\;t\in[0,1]` |
| ODE 方程组 | `\begin{cases}\frac{dx}{dt}=y\\\frac{dy}{dt}=-x\\x(0)=0\\y(0)=1\end{cases},\;t\in[0,1]` | `\frac{dx}{dt}=y,\;x(0)=0` |

优化变量必须在下标中声明，每个数值变量必须通过 request binding 提供初值。高阶 ODE 的 `y(0)`、`y'(0)`、... 分别映射到规范化一阶状态 `y`、`y_d1`、...；直到 `n-1` 阶的初值必须齐全。ODE 必须给出自变量区间和每个状态的初值；不解析 DAE 或边值条件。

## 8. 白名单环境与组合规则示例

| 规则 | 合法输入 | 非法输入 |
|---|---|---|
| 花括号/圆括号 | `{x+1}`, `(x+1)`, `\left(x+1\right)` | `{x+1`, `\left(x+1)` |
| 显式乘除 | `2*x`, `2\cdot x`, `2\times x`, `x/2` | `2**x`, `x//2` |
| `matrix` | `\begin{matrix}1&2\\3&4\end{matrix}` | `\begin{matrix}1&2\end{pmatrix}` |
| `bmatrix` | `\begin{bmatrix}1&0\\0&1\end{bmatrix}` | `\begin{vmatrix}1\end{vmatrix}` |
| relation list | `x+y=2,x-y=0` | `x+y=2,` |
| `cases` relation set | `\begin{cases}x+y=2\\x-y=0\end{cases}` | `\begin{cases}x+y\end{cases}` |
| matrix builtin | `\operatorname{rank}(A)` | `\operatorname{svd}(A)` |

每个表格行是一类白名单规则；`parser_tests` 必须至少使用该行正反例，并为 EBNF 中其他分支增加覆盖。

## 9. Operation 覆盖

显式 operation 为 `AUTO`、`EVALUATE`、`SIMPLIFY`、`EXPAND`、`FACTOR`、`DIFFERENTIATE`、`INTEGRATE`、`LIMIT`、`SUM`、`PRODUCT`、`SOLVE`、`LINEAR_ALGEBRA`、`OPTIMIZE`、`ODE_IVP` 和 `DEFINE`。`AUTO` 从顶层结构推断；显式 operation 与输入顶层结构冲突时返回 `OPERATION_MISMATCH`。

示例：`SOLVE` 加 `x^2=4` 合法；`ODE_IVP` 加 `x+1` 非法。`EVALUATE` 加 `x:=2` 非法，必须使用 `DEFINE` 或 `AUTO`。

## 10. AST 调试格式

`--debug`/`-d` 在 stderr 输出缩进树，每行固定为 `Kind [begin,end) payload`，子节点按源码顺序排列。字符串使用 JSON 风格转义，数值保留源码拼写。该格式用于调试和黄金测试，不属于 C ABI，也不承诺跨主版本兼容。
