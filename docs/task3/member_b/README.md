# Task 3 图与文档：UC02、UC03、UC06、UC10、UC11

负责人：Zhang Yikai（Member B，学号 1240007611）　交付给：Zhang Yuhua（汇总 PDF）　核对：全员

## 一、清单

| 编号 | 图源（可编辑）与导出 | 对应 | 内容 |
|---|---|---|---|
| Fig. B1 | `figures/figb1_uc02_search_view_courses.svg` / `.png` | UC02 | 关键词搜索命中与无匹配两条分支，详情放在"有匹配"分支里的 opt 片段 |
| Fig. B2 | `figures/figb2_uc03_view_available_seats.svg` / `.png` | UC03 | 先查教学班是否存在，存在才读容量、数有效人数、算余量 |
| Fig. B3 | `figures/figb3_uc06_view_timetable.svg` / `.png` | UC06 | 取本人 active 选课，循环取时段，拼周课表；无选课给空课表 |
| Fig. B4 | `figures/figb4_uc10_view_enrollment_count.svg` / `.png` | UC10 | 教师查本人教学班人数与余量，他人教学班返回 PermissionDenied |
| Fig. B5 | `figures/figb5_uc11_view_student_list.svg` / `.png` | UC11 | 同一权限检查后按 active 选课列学生名单（学号、姓名） |

`Team4_Zhang_Yikai_Task3_Figures_and_Text_EN.docx` 是这五张图的英文说明（含图注、逐图描述，以及第 6 节按评审改了什么的记录）。

## 二、怎么看

- 自上而下读：实线是调用，虚线是返回；`alt` 的第二个操作数是失败分支，`opt` 是"只有满足这个条件才走的可选片段"。
- B1 里 `opt [student selects a course]` 是关键：只有学生从搜索结果里选中一门课才进详情页，没搜到就停在 `'No course matches the keyword.'`。
- B2 里先 `findTeachingClass`，`alt` 判存在；不存在直接 `RecordNotFound` 结束，存在才 `getCapacity` 和数人数。

## 三、术语

全组统一用 **active**，指"当前有效选课"，即代码里的 `EnrollmentStatus::Active`。
`dropped` 记录留作历史，不占名额。图上不再出现 enrolled，B3、B5 里的自消息也改成了 `keep status = active only`。

## 四、按 11 月 5 日评审改的三处

1. UC03：原来先读容量、数人数、算余量，最后才判教学班存在，教学班不存在时根本取不到容量。现在先查教学班，不存在就提示并结束。这个顺序和代码里 `view_available_seats()` 的实际顺序也一致。
2. UC02：原来"打开一门课程"画在两个分支之后，读起来像没搜到也能进详情。现在放进"有匹配"分支，用 `opt` 包住，失败分支只到提示语结束。
3. 术语：enrolled 全部换成 active，五张图与文档一起改。
   另外自查时发现 B5 原来返回 `studentNo, name, enrolledOn`，但代码里 `RosterEntry` 只有学号和姓名，没有选课时间，图上的第三个字段已删除。

## 五、可编辑源与导出方式

- `.svg` 是可编辑矢量源，Inkscape、draw.io、Illustrator 都能打开继续改；`.png` 是 2 倍导出（Chrome 无头截图，`--force-device-scale-factor=2`），插进 A4 报告按 15.5 cm 宽排版仍然清楚。
- 生成脚本没有放进仓库：脚本里写的是本机绝对路径，别人拉下来跑不起来。要放的话得先改成相对路径，需要我再提交。

## 六、与其他人图的一致性

- 五张图都是只读，不写 `Enrollment`，不碰 `enroll()` 的五步检查。
- 名额只有 `capacity - activeCount` 一个公式，现算，没有第二个计数器。
- 课表由 active 选课派生，不单独存。
- UC10 / UC11 画的是 Zhu Ziliang 的 `TeacherService`（`class_summary` / `class_roster`），学生模块里没有重复实现一套计数逻辑。
- 类名、用例名与 Task 2 用例图、`docs/task3/FIGURES.md` 保持一致。

## 七、还没闭合的两项

- 图号 B1–B5 是暂定，汇总成一份 PDF 时按整体重新编号更省事；重新编号只改图下标题，不动图内文字。
- Task 2 的完整用例图原件不在我这份材料里（我这里只有类图和时序图），需要从提交记录或组员 A 那边取，报告里放原图、这次不重画。
