# Task 3 图说明：对象图与 UC09/13/14/15/16 时序图

负责人：Zhu Ziliang　　交付给：Zhang Yuhua（汇总 PDF）　　核对人：Zhang Yikai（术语）

## 一、清单

| 编号 | 文件（可编辑源文件 + 高清导出） | 对应 | 内容 |
|---|---|---|---|
| Figure 1 | `figures/fig1_object_diagram_state.drawio` / `.png` | 对象图 | 2026-11-03 10:05 时选课状态的具体实例与链接 |
| Figure 2 | `figures/fig2_uc09_view_assigned_teaching_classes.drawio` / `.png` | UC09 | 教师查询本人负责的教学班、名单与人数 |
| Figure 3 | `figures/fig3_uc13_manage_courses_and_teaching_classes.drawio` / `.png` | UC13 | 管理员新增课程/教学班及失败返回 |
| Figure 4 | `figures/fig4_uc14_set_teaching_class_capacity.drawio` / `.png` | UC14 | 管理员改容量，检查现有有效选课 |
| Figure 5 | `figures/fig5_uc15_set_registration_period.drawio` / `.png` | UC15 | 管理员设置选课时段，拒绝无效时段 |
| Figure 6 | `figures/fig6_uc16_manage_student_teacher_information.drawio` / `.png` | UC16 | 管理员维护学生/教师资料，拒绝重复与空字段 |

生成方式：`python docs/task3/make_diagrams.py`。`.drawio` 可用 draw.io / diagrams.net 打开继续修改，
`.png` 为 3 倍比例渲染（约 675–1368 单位画板，长边约 2000–4100 像素），插入报告缩小后仍清晰。
图内文字用英文，与 Task 2 用例名和代码类名一致；同一条术语（`TeachingClass`、`Enrollment`、
`RegistrationRule`）在图中、代码与测试中含义完全相同。

## 二、逐图说明（中英双语）

### Figure 1　对象图：教学班 TC-10 的选课状态

**中文**：本图取 2026-11-03 10:05 的一个具体时刻，画出 8 个对象实例及其属性值与链接：
学生 alice、bob，教师 dr_wang，课程 se101，教学班 tc10，选课记录 e1、e2，以及当时生效的选课规则 r1。
tc10 的 `capacity = 1`，只有 e1 是 `active`，e2 已是 `dropped`，因此 `remaining_seats = 1 - 1 = 0`，
与教师端和选课检查看到的结果一致；这条关系正是团队说明要求"容量与选课记录一致"的检查点。
时间线上，bob 在 10:00 占到这个唯一名额、10:01 退课让出，alice 才能在 10:02 选进来，
因此 10:05 的状态与余量 0 并不矛盾。e2 保留 `dropped` 作为历史，不占名额；
一对多链接（Course 与 TeachingClass、
Student 与 Enrollment）也可从图中直接读出。

**English**: The diagram shows one concrete moment (2026-11-03 10:05) with eight instance
objects and their attribute values: students `alice` and `bob`, teacher `dr_wang`, course
`se101`, teaching class `tc10`, enrollments `e1`/`e2`, and the registration rule `r1` in force.
`tc10` has `capacity = 1` and only `e1` is `active`, while `e2` is already `dropped`, so
`remaining_seats = 1 - 1 = 0`, which is exactly what the teacher view and the enrollment check
report. The timeline is part of the figure: bob held the only seat from 10:00, dropped it at
10:01 and freed it, so alice could enroll at 10:02. `e2` keeps `dropped` as history, does not
occupy a seat, and the one-to-many links
(Course to TeachingClass, Student to Enrollment) can be read directly from the diagram.

### Figure 2　UC09 View Assigned Teaching Classes

**中文**：教师打开"我的教学班"，页面把登录得到的身份交给 `TeacherService`；服务先校验角色，
再向 `SystemContext` 取教学班快照，并在循环中对每个教学班调用 `enrollmentCount` 现算人数，
最后返回只属于本人的教学班摘要。图中 `alt` 片段给出失败路径：请求他人教学班时返回
`PermissionDenied`，页面提示拒绝且不显示任何教学班内容，对应"教师只读本人负责的教学班"这条要求。

**English**: The teacher opens "my teaching classes" and the page passes the identity obtained at
login to `TeacherService`. The service checks the role first, then obtains the teaching-class
snapshot from `SystemContext`, calls `enrollmentCount` for each class in a loop so that counts are
always computed from active enrollments, and returns only the classes assigned to this teacher.
The `alt` fragment shows the failure path: a request for another teacher's class returns
`PermissionDenied`, the page reports the rejection, and no class content is disclosed.

### Figure 3　UC13 Manage Courses and Teaching Classes

**中文**：管理员提交教学班表单后，`AdministratorService` 依次校验角色、课程是否存在、教师是否存在，
再校验上课时段、教师时段冲突与编号重复；全部通过才写回目录并同步给选课服务。
失败分支返回 `InvalidInput` / `DuplicateIdentifier` / `RecordNotFound` / `TimeConflict` 之一，
表单保持打开且不写入任何数据，这与"管理员增删改课程与教学班时检查关系"的自查项一致。
`enroll()` 不参与本用例，因此不会新增或修改任何选课记录。

**English**: After the administrator submits the teaching-class form, `AdministratorService`
checks the role, then whether the course and the teacher exist, then the class meetings, the
teacher's timetable conflict and duplicate identifiers. Only when every check passes does it write
the catalog back and synchronise the registration service. The failure branch returns one of
`InvalidInput` / `DuplicateIdentifier` / `RecordNotFound` / `TimeConflict` and writes nothing,
which matches the self-check item on course/teaching-class relationships. `enroll()` is not
involved here, so no enrollment record is created or changed.

### Figure 4　UC14 Set Teaching Class Capacity

**中文**：管理员把教学班容量改成新值前，服务先取当前有效选课人数；若新容量小于人数，
返回 `CapacityBelowEnrollment` 并说明差多少，容量与已有选课记录都不变。
只有在人数不超过新容量时才写回，并由选课服务按 `capacity - active count` 重新得出剩余名额，
所以教师端人数、学生端余量与管理员看到的数字同步变化，不需要再存一份计数。
图里不写死同一组数字：成功分支写 `remainingSeats = newCapacity - activeCount`，
并在注释中给出示例（activeCount = 2 时，newCapacity = 1 被拒绝、newCapacity = 3 得余量 1），
避免失败分支与成功分支互相矛盾。图中的教学班编号是 TC-20，与对象图里的 TC-10 不是同一时刻的数据，属于独立示例。

**English**: Before changing a capacity, the service reads the current number of active
enrollments. If the new capacity is smaller than that number it returns
`CapacityBelowEnrollment`, states the difference, and leaves both the capacity and the existing
records unchanged. Only a capacity that is at least the active count is written back, and the
registration service then recomputes the remaining seats as `capacity - active count`, so the
teacher count, the student view and the administrator view change together without storing a
second counter. The success branch states `remainingSeats = newCapacity - activeCount`
instead of a hard-coded number, and a note gives one example: with activeCount = 2,
newCapacity = 1 is rejected while newCapacity = 3 gives 1 remaining seat. The class in this
figure is TC-20, an independent example and not the TC-10 snapshot of the object diagram.

### Figure 5　UC15 Set Registration Period

**中文**：管理员提交开始与结束时间后，服务校验时间格式与先后顺序，格式错误或起止颠倒
返回 `InvalidInput` 并保留原规则；合法时写回规则并立即按"
`now >= opens_at` 与 `now < closes_at`
"换算出 `registration_open`。选课与退课仍只读这一个布尔值，Yuhua 的检查顺序不受影响，
因此时段改动不会绕过任何一条选课规则。
时段状态按请求时刻现算，不在保存时只算一次，否则时间走过关闭时刻后仍会停在旧值；
管理员手动关闭会清空起止时间，避免下一次现算又被时段打开。

**English**: After the administrator submits the opening and closing time, the service validates
the format and the ordering; a malformed timestamp or `start >= end` returns `InvalidInput` and
keeps the previous rule. A valid period is written back and `registration_open` is derived from
two comparisons, `now >= opens_at` and `now < closes_at`. The state is evaluated at request
time instead of being cached when it is saved, otherwise it would stay stale once the closing
time has passed; closing registration manually clears the window. Enrollment and drop still
read only that boolean, so Yuhua's check order is untouched and changing the window cannot
bypass any rule.

### Figure 6　UC16 Manage Student / Teacher Information

**中文**：管理员新建学生账号时，服务先校验角色，再按编号和学号各查一次是否已存在，
最后校验编号为正、学号与姓名非空；重复或空字段分别返回 `DuplicateIdentifier` / `InvalidInput`，
不写入任何记录。通过校验后只写入账号资料，已选课记录保持不变。
本图以“创建学生”为例；创建教师走类似流程（`createTeacher`），而修改（`updateStudent` / `updateTeacher`）与删除（`removeStudent` / `removeTeacher`）各有自己的检查：删除仍负责教学班的教师、或仍有有效选课的学生，都会因 `CatalogInUse` 被拒绝。

**English**: When the administrator creates a student account, the service checks the role, looks
the identifier up by `student_id` and by `student_number`, and then validates that the id is
positive and that the number and name are not blank; duplicates and blank fields return
`DuplicateIdentifier` / `InvalidInput` and nothing is written. Once the checks pass only the
account record is stored and existing enrollments stay untouched. The diagram uses student
creation as an example; teacher creation follows a similar process, while updates
(`updateStudent` / `updateTeacher`) and removals (`removeStudent` / `removeTeacher`) have their own
checks. Removal is refused with `CatalogInUse` while the teacher still teaches a class or the
student still has active enrollments.

## 三、自查（对应 Task 3 手册第四节）

- [x] 图用标准工具绘制并保留可编辑源文件（`.drawio`，非类图改名的图片）。
- [x] 对象图是具体实例与属性值，不是类图；链接与多重性可从图中读出。
- [x] 5 张时序图与 UC09、UC13、UC14、UC15、UC16 一一对应，标题写明编号与用例名。
- [x] 每张图都有明确的触发、交互和返回；UC09/13/14/15/16 均含失败分支。
- [x] 只依据 URS 与 Task 2 已确认用例建模，未加入推荐、审批、导入或复杂统计。
- [x] 导出图缩小到报告尺寸后仍可读（3 倍渲染，最小字号约 9 单位）。
- [x] 教师只读权限、课程/教学班关系、容量与选课记录一致性三点在图 2、图 3、图 1和图 4 中可验证。

## 四、交接说明

- 图源在 `docs/task3/figures/`，可直接插入报告；`.drawio` 供后续修改，改完重跑
  `make_diagrams.py` 会覆盖同名文件，请不要在图源里手工改坐标后忘记同步脚本。
- 图内保留英文标签，若组长最终 PDF 需要中文图注，只在图下标题处翻译，不要改图内术语，
  以免与 Task 2 用例图不一致。
- 报告编号：本组 6 张图按 Figure 1–6 排列；若与 Yikai/Yuhua 的图合并后需要重新编号，
  只改标题文字，不影响图源。


## 五、术语统一：有效选课状态用 `active`

- 代码里的枚举是 `EnrollmentStatus::Active` / `EnrollmentStatus::Dropped`，
  见 `header_files/course_registration_business_rules.hpp`，对象图与测试都按这个取值绘制。
- 因此建议全组统一写 **`active` / `dropped`**；若另一份图用的是 `enrolled`，
  建议改那一份的图，而不是改代码枚举（改枚举会牵动头文件、测试、接口文档和四张图）。
- 如果三人最终决定用 `enrolled`，需要同步修改的位置：
  `EnrollmentStatus` 枚举、`check_data_integrity` 与测试断言里的词、对象图属性值、
  `docs/interfaces.md` 的 `EnrollmentStatus` 一行、以及另一份图的 `active`。
