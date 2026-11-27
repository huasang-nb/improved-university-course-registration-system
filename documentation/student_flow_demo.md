# 学生流程演示（第 5 周）

> 负责人：Zhang Yikai。对应飞书 `Code-Week5-2`：学生流程集成修复与演示数据。

## 目的

把学生端从「登录 → 搜索 → 选课 → 课表 → 个人状态 → 退课」整条链路串起来，
并验证每一步都走同一份共享状态和持久化文件。

## 编译与运行

```powershell
powershell -ExecutionPolicy Bypass -File .\build_and_test.ps1
.\build\student_flow_demonstration.exe --state build\demo_flow\state.json
```

可选参数：

```text
--account S2023001     演示账号（默认 S2023001）
--password Demo2026!   演示密码（默认 Demo2026!）
```

`--state` **必须显式指定**，演示不会写入默认的 `data/state.json`。
文件不存在时用种子数据初始化；已存在时读取原状态（可重复运行）。

## 演示流程与预期输出

```text
== 1. 登录 ==
   logged in as S2023001
== 2. 搜索 "SE" ==
   SE101 Software Engineering（3 学分）
      班 10 / Dr. Wang / 余 2
      班 11 / Dr. Zhao / 余 2
== 3. 选课（班 10） ==
   Enrollment successful
== 4. 我的课表 ==
   周1 540-600 SE101 A101
== 5. 个人状态 ==
   有效 1 门，已退 0 门
== 6. 退课（班 10） ==
   Drop successful
== 7. 退课后状态 ==
   有效 0 门，已退 1 门
```

再次运行同一个 `--state` 文件会基于已有状态继续：重新选课时把该班从
`Dropped` 改回 `Active`，退课后第 7 步仍回到「有效 0 门，已退 1 门」，
说明文件可复用、退课历史不会丢。

## 登录与密码说明

- 演示使用 `LoginService` + 固定校验器（所有演示账号共用 `Demo2026!`），
  **不读取 `password_hash`**；
- 正式登录应由 libsodium 校验 `state.json` 里的 Argon2id PHC 字符串，
  接入方式见 `documentation/persistence_design.md` 与登录模块说明；
- 演示账号的 `password_hash` 为空（未设置密码），这与“旧账号需要管理员重置”
  的设计一致。

## 演示数据

- 使用 `make_seed_context()` 的种子数据：3 门课程、4 个教学班、2 名学生、
  3 名教师、1 名管理员；
- `state.json` 的 `schema_version` 为 2，人员记录含 `password_hash` 字段；
- 演示目录属于构建产物，不进入提交。
