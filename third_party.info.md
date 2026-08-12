## 第三方库信息

| 包名         | 版本   | 依赖     | 编译选项    |
| ------------ | ------ | -------- | ----------- |
| sundials     | 7.5.0  | /        | 静态        |
| boost        | 1.86.0 | /        | 静态        |
| gsl          | 2.7.1  | /        | 静态        |
| mpfr         | 4.2.0  | gmp      | 静态        |
| symengine    | 0.14.0 | gmp      | 静态        |
| gmp          | 6.3.0  | /        | 静态        |
| ceres-solver | 2.2.0  | eigen    | 静态        |
| eigen        | 3.4.0  | /        | header-only |
| nlopt        | 2.10.0 | /        | 静态        |
| armadillo    | 12.6.4 | openblas | 静态        |
| openblas     | 0.3.25 | /        | 静态        |
| ginac        | 1.8.9  | cln      | 静态        |
| cln          | 1.3.7  | gmp      | 静态        |


| 功能类别       | 推荐库       | 备选库     | 职责范围                     |
| -------------- | ------------ | ---------- | ---------------------------- |
| **线性代数**   | Eigen        | Armadillo  | 矩阵运算、线性方程组、特征值 |
| **符号计算**   | SymEngine    | GiNaC      | 符号化简、微分、部分积分     |
| **数值积分**   | GSL          | Boost.Math | 定积分、ODE 求解             |
| **高精度计算** | MPFR         | GMP        | 任意精度浮点                 |
| **非线性优化** | Ceres Solver | NLopt      | Level 1 非线性方程组         |
| **微分方程**   | Sundials     | -          | ODE/DAE 系统                 |
