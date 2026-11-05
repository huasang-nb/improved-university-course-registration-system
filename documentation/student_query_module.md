# 学生查询模块（UC02 / UC03 / UC06）—— Zhang Yikai

> 状态：**已实现并通过编译与测试，待 Zhu Ziliang 复核后合并**。
> 对应 `documentation/interfaces.md` 第 3.2 节（课程搜索与详情）与第 3.5 节的余量算法。
> 本文件只描述本模块，不改动共享头文件里的任何既有定义。

## 一、这个模块负责什么

| 用例 | 功能 | 入口 |
|---|---|---|
| UC02 Search / View Courses | 关键词搜索课程（课程代码、课程名称、教师姓名）、按代码/学分/教师筛选、查看课程详情与教学班 | `StudentQueryService::search_courses` / `course_detail` |
| UC03 View Available Seats | 查看某个教学班的容量、有效人数、剩余名额 | `StudentQueryService::view_available_seats` |
| UC06 View Timetable | 学生查看本人课表（只含有效选课，按星期与开始时间排序） | `StudentQueryService::view_timetable` |

对应分工里的 FR-02、FR-03、FR-05、FR-09，编号与条目的逐条对照等 URS 定稿后补齐。

**不在这里做的事**：不创建、不修改、不删除 `Enrollment`；不改课程目录；不做选课的五个检查。
选课仍走 `RegistrationService::enroll()`。本模块是只读的，所以不会和组长的业务规则抢写权。

## 二、数据结构

| 结构 | 字段 | 说明 |
|---|---|---|
| `CourseSearchFilter` | `course_code`、`credits`、`teacher_id` | 留空 / -1 表示这一项不筛选 |
| `TeachingClassView` | `teaching_class_id`、`teacher_id`、`teacher_name`、`capacity`、`active_enrollments`、`remaining_seats`、`meetings` | 课程详情里的教学班摘要 |
| `CourseView` | `Course course`、`vector<TeachingClassView>` | 一门课程与它下面的教学班 |
| `SeatsView` | `teaching_class_id`、`course_id`、`course_code`、`course_name`、`capacity`、`active_enrollments`、`remaining_seats` | UC03 的返回 |
| `TimetableEntry` | `teaching_class_id`、`course_code`、`course_name`、`teacher_name`、`meeting` | 课表的一行 |

这些结构只存在于本模块，不改 `Course`、`TeachingClass`、`Enrollment` 的既有定义。

## 三、和共享数据的关系（兼容性说明）

1. **数据只有一个来源**：课程、教学班、选课记录、规则全部从 `SystemContext` 读，
   本模块不缓存副本，也不另建 `Course` / `TeachingClass`。
2. **名额只算一次公式**：`remaining_seats = capacity - active 记录数`，用的是
   `SystemContext::enrollment_count()`，和 `TeacherService::class_summary()`、
   `RegistrationService::remaining_seats()` 是同一批 `Enrollment` 记录。
   系统里没有第二个计数器，所以学生看到的余量、教师看到的人数、管理员看到的人数不会互相矛盾。
3. **课表是派生结果**：由 `active_enrollments(student_id)` 加上教学班的 `meetings` 拼出来，
   数据库里不存在"课表"这张表。退课之后记录变 `Dropped`，课表自然就少一行。
4. **对别人的文件只做追加**：`CMakeLists.txt` 与 `build_and_test.ps1` 只在末尾加了两个目标、
   一个测试名，原有目标名与依赖没动。共享头文件一个字没改，所以不需要走
   `interface_change_request.md` 的破坏性改动流程。
5. **"现在是否开放选课"不要读保存下来的布尔值**：本模块的三个用例（UC02 / UC03 / UC06）都不展示
   选课状态，所以现在没有依赖这块数据。以后网页要显示，每次渲染时调用
   `context.registration_window_open()`——它按当前时间现算，只有管理员没设起止时间时才回落到
   `registration_open` 这个手动开关。不要直接读 `context.rule().registration_open`：那是保存值，
   管理员设了 09:00–11:00 之后，12:00 它可能还是 `true`，页面会写着"开放中"，学生一点却被拒。
   需要固定时刻做测试时用 `context.registration_window_open_at("2026-11-02T08:00")`。
   `StudentQueryService` 持的是 `const SystemContext&`，这两个接口都是 `const`，以后要加一个
   "当前是否开放"的只读字段也不用改构造函数。

## 四、行为细节

### 四个接口共用的一条约定：输出参数先清空

`search_courses`、`course_detail`、`view_available_seats`、`view_timetable` 都是
**进来先把输出参数清空，失败时留空**。所以"先查存在的课程、再用同一个变量查不存在的课程"
不会把上一门课的内容带出来；查不到、没权限、查无此人时拿到的是空结构，不是旧数据。

为什么选这个做法，而不是只在文档里写"调用方必须先看结果码"：网页那边一旦漏了判断，
只写约定的话页面会把旧课程当新课程显示，是错误的数据；清空之后最坏也只是显示一个空页面，
不会误导人。代价是几乎为零——正常调用方本来就要先看 `OperationResult`。

但要说明白：**清空只是兜底，不是"可以不看结果码"的许可**。成功与否一律以
`OperationResult::code` 为准，界面该提示的 `RecordNotFound`、`PermissionDenied` 照常提示。
对应的测试就在测试文件第 9 组：成功查一次、再用同一变量查不存在的，逐个字段检查有没有残留。

### 搜索（UC02）

- 关键词在**课程代码、课程名称、教师姓名**三处做不区分大小写的包含匹配。
  空关键词表示不过滤，列出全部课程。
- 筛选条件叠加在关键词之上：课程代码按忽略大小写的**完全相等**比较，学分为精确值，
  教师按 `teacher_id` 过滤，并且课程详情里只保留这位教师的教学班。
- 没有匹配时返回 `Success` + 空列表，不是错误。界面按用例要求提示
  `No course matches the keyword.`。
- 结果按 `course_code` 排序，教学班按 `teaching_class_id` 排序，同样的数据每次顺序一致。

### 课程详情（UC02）

- 课程不存在返回 `RecordNotFound`。
- 返回该课程的全部教学班：教师姓名、容量、有效人数、剩余名额、上课时间与教室。

### 剩余名额（UC03）

- 教学班不存在返回 `RecordNotFound`，与 `RegistrationService::remaining_seats()` 返回 `-1`
  的做法区分开：那个接口是给内部算余量用的，这里是给人看的结果对象。

### 课表（UC06）

- 只允许 `Role::Student`，其他角色返回 `PermissionDenied`；学生编号在账号表里查不到返回
  `RecordNotFound`。
- 只取 `Active` 记录，已退课的不出现。
- 一条选课记录对应的教学班在目录里查不到时**跳过这一条**，不让一条脏数据把整张课表打不开；
  这种不一致由 `AdministratorService::check_data_integrity()` 报出来。
- 排序：`day_of_week` → `start_minute` → `teaching_class_id`。
- 一条有效选课都没有时返回 `Success` + 空列表，界面提示
  `You have not enrolled in any teaching class.`。

## 五、UC10 / UC11 为什么不在这个模块里

UC10（查看教学班人数）和 UC11（查看学生名单）已经由 Zhu Ziliang 的
`TeacherService::class_summary()` 与 `class_roster()` 实现，权限校验（只读本人教学班、
越权返回 `PermissionDenied`）、只列 `active`、按学号排序都在那边。

Task 3 里我画的两张图（Fig. B4、B5）画的就是 `TeacherService`，所以代码侧不在学生模块里
重复实现一遍。两套并存会多出第二个计数来源，正好违反"名额只存一个算法"的约定。
如果组内决定把这两个用例挂到学生模块名下，只需在 `StudentQueryService` 加一层转发，
不要另写一份计数逻辑。

## 六、编译与测试

```powershell
# 一次跑完全部编译与三套测试
.\build_and_test.ps1

# 只跑本模块的测试
g++ -std=c++17 -Wall -Wextra -Iheader_files `
    source_code/course_registration_business_rules.cpp `
    source_code/teacher_and_administration_services.cpp `
    source_code/student_query_and_timetable_services.cpp `
    automated_tests/student_query_and_timetable_tests.cpp `
    -o build/student_query_and_timetable_tests.exe
.\build\student_query_and_timetable_tests.exe
```

测试覆盖：大小写不敏感搜索、教师姓名命中、无匹配返回空、三种筛选、课程详情与
`RecordNotFound`、名额随选课/退课变化、课表排序与退课后消失、非学生角色被拒、
学生编号不存在。构建在 `-Wall -Wextra` 下零警告。

`CMakeLists.txt` 里的三个目标（库、演示、测试）按现有写法追加，本机没有安装 CMake，
**尚未用 CMake 配置验证过**；当前以 `build_and_test.ps1` 的 g++ 直接编译为准，
装了 CMake 的组员可以顺手确认一下。

演示程序（输出可直接贴进飞书）：

```powershell
.\build\student_query_demonstration.exe
```

## 七、当前未完成

- 课表与详情还没有持久化读入，仍由 `SystemContext` 用内存示例数据构造（阶段二再做）。
- HTTP / Web 页面未接入（计划 11 月 16 日至 22 日）。
- 关键词搜索是线性扫描，数据量上去以后可以加索引，但当前课程规模不需要。
- FR 编号与条目的逐条对照待 URS 定稿。
