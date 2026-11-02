# -*- coding: utf-8 -*-
"""生成 Task 3 中 Zhu Ziliang 负责的 6 张图（Zhu Ziliang）。

运行：python make_diagrams.py
输出到 figures/ 目录：
  fig1_object_diagram_state.drawio / .png
  fig2_uc09_view_assigned_teaching_classes.drawio / .png
  fig3_uc13_manage_courses_and_teaching_classes.drawio / .png
  fig4_uc14_set_teaching_class_capacity.drawio / .png
  fig5_uc15_set_registration_period.drawio / .png
  fig6_uc16_manage_student_teacher_information.drawio / .png

图内文字用英文，与 Task 2 用例名及代码中的类名一致；
逐图说明见同目录 FIGURES.md（中英双语）。
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from umlkit import ObjectDiagram, SequenceDiagram  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "figures")
SCALE = 3.0


def save(diagram, filename, drawio_name):
    png = os.path.join(OUT, filename + ".png")
    drawio = os.path.join(OUT, filename + ".drawio")
    size = diagram.save(png, drawio, drawio_name, scale=SCALE)
    print("%-58s %s" % (filename, size))


# ---------------------------------------------------------------------------
# 图 1：对象图（某个具体时刻的实例与链接）
# ---------------------------------------------------------------------------

def figure1_object_diagram():
    diagram = ObjectDiagram(
        "Figure 1  Object diagram: registration state of teaching class TC-10",
        "Snapshot at 2026-11-03 10:05, registration open (2026-11-02 08:00 - 2026-11-13 18:00)")

    diagram.object(
        "dr_wang", 60, 70, "dr_wang : Teacher",
        ['teacher_id = 100', 'teacher_number = "T2015007"',
         'name = "Dr. Wang"', 'department = "Software Engineering"',
         "account_active = true"])
    diagram.object(
        "tc10", 420, 200, "tc10 : TeachingClass",
        ['teaching_class_id = 10', 'course_id = 1', 'teacher_id = 100',
         "capacity = 1", 'day_of_week = 1 (Monday)',
         'start_minute = 540, end_minute = 600', 'room = "A101"'],
        width=270)
    diagram.object(
        "se101", 880, 70, "se101 : Course",
        ['course_id = 1', 'course_code = "SE101"',
         'course_name = "Software Engineering"', "credits = 3"],
        width=250)
    diagram.object(
        "e1", 900, 320, "e1 : Enrollment",
        ["student_id = 1001", "teaching_class_id = 10",
         "status = active", 'enrolled_at = "2026-11-03T10:02"'],
        width=250)
    diagram.object(
        "alice", 900, 500, "alice : Student",
        ["student_id = 1001", 'student_number = "S2023001"',
         'name = "Alice Chen"', 'major = "Software Engineering"',
         "account_active = true"],
        width=250)
    diagram.object(
        "e2", 420, 470, "e2 : Enrollment",
        ["student_id = 1002", "teaching_class_id = 10",
         'enrolled_at = "2026-11-03T10:00"', "status = dropped",
         'dropped_at = "2026-11-03T10:01"'],
        width=270)
    diagram.object(
        "bob", 420, 650, "bob : Student",
        ["student_id = 1002", 'student_number = "S2023002"',
         'name = "Bob Liu"', 'major = "Software Engineering"',
         "account_active = true"],
        width=270)
    diagram.object(
        "r1", 60, 470, "r1 : RegistrationRule",
        ["registration_open = true", "maximum_credits = 18",
         'opens_at = "2026-11-02T08:00"',
         'closes_at = "2026-11-13T18:00"'],
        width=250)

    diagram.link("tc10", "dr_wang", label="teacher", path=[
        (420, 240), (340, 240), (340, 131), (300, 131)])
    diagram.link("tc10", "se101", label="course", path=[
        (690, 240), (790, 240), (790, 105), (880, 105)])
    diagram.link("e1", "tc10", label="teaching class", path=[
        (900, 360), (790, 360), (790, 290), (690, 290)])
    diagram.link("e1", "alice", label="student", path=[
        (1025, 426), (1025, 500)])
    diagram.link("e2", "tc10", label="teaching class", path=[
        (555, 470), (555, 357)])
    diagram.link("e2", "bob", label="student", path=[
        (555, 593), (555, 650)])
    diagram.link("r1", "tc10", label="in force for", path=[
        (310, 520), (380, 520), (380, 320), (420, 320)])

    diagram.note(
        900, 665, 380, 58,
        "Derived value shown by the teacher and administrator views:\n"
        "remaining_seats = capacity(1) - active enrollments(1) = 0")
    diagram.note(
        60, 640, 340, 88,
        "Timeline: bob took the only seat at\n"
        "10:00 and dropped it at 10:01, which\n"
        "freed the seat; alice could enroll at\n"
        "10:02. At 10:05 the state is capacity 1,\n"
        "one active enrollment, 0 remaining seats.")
    save(diagram, "fig1_object_diagram_state",
         "Fig1 Object diagram - registration state")


# ---------------------------------------------------------------------------
# 图 2：UC09 View Assigned Teaching Classes
# ---------------------------------------------------------------------------

def figure2_uc09():
    diagram = SequenceDiagram(
        "Figure 2  UC09 View Assigned Teaching Classes",
        "Teacher reads only the teaching classes assigned to him/her; the identity comes from the login module")

    teacher = diagram.participant("Teacher", kind="actor")
    page = diagram.participant("TeacherPage")
    service = diagram.participant("TeacherService")
    context = diagram.participant("SystemContext")
    registration = diagram.participant("RegistrationService")

    m = []
    m.append(diagram.message(teacher, page, "openMyTeachingClasses()"))
    m.append(diagram.message(page, service,
                             "assignedTeachingClasses(identity)"))
    m.append(diagram.self_message(service, "checkRole(identity)"))
    m.append(diagram.message(service, context, "listTeachingClasses()"))
    m.append(diagram.message(context, registration,
                             "teachingClassSnapshot()"))
    m.append(diagram.ret(registration, context, "classes"))
    m.append(diagram.ret(context, service, "classes"))
    m.append(diagram.message(service, context, "enrollmentCount(classId)"))
    m.append(diagram.ret(context, service, "activeCount"))
    m.append(diagram.self_message(service, "buildSummary(class)"))
    m.append(diagram.ret(service, page, "summaries of own classes"))
    m.append(diagram.message(page, teacher, "render class list + counts", kind="async"))

    # 循环取每个教学班的人数
    diagram.fragment("loop", "[for each teaching class of this teacher]",
                     m[7], m[9])

    base = len(m)
    m.append(diagram.message(page, service,
                             "classRoster(identity, otherClassId)"))
    m.append(diagram.ret(service, page, "PermissionDenied"))
    m.append(diagram.message(page, teacher,
                             'show "permission denied"', kind="async"))

    # 越权读取的备选路径
    diagram.fragment("alt", "[class belongs to another teacher]", base, base + 2)

    diagram.note(service, "role is checked before any class data is read",
                 m[2], width=210)

    save(diagram, "fig2_uc09_view_assigned_teaching_classes",
         "Fig2 UC09 View Assigned Teaching Classes")


# ---------------------------------------------------------------------------
# 图 3：UC13 Manage Courses and Teaching Classes
# ---------------------------------------------------------------------------

def figure3_uc13():
    diagram = SequenceDiagram(
        "Figure 3  UC13 Manage Courses and Teaching Classes",
        "Administrator creates a teaching class; every failed check returns a reason and writes nothing")

    admin = diagram.participant("Administrator", kind="actor")
    page = diagram.participant("AdminPage")
    service = diagram.participant("AdministratorService")
    context = diagram.participant("SystemContext")
    registration = diagram.participant("RegistrationService")

    m = []
    m.append(diagram.message(admin, page, "submit teaching class form"))
    m.append(diagram.message(page, service,
                             "createTeachingClass(identity, data)"))
    m.append(diagram.self_message(service, "canManageCatalog(identity)"))
    m.append(diagram.message(service, context, "findCourse(courseId)"))
    m.append(diagram.ret(context, service, "course"))
    m.append(diagram.message(service, context, "findTeacher(teacherId)"))
    m.append(diagram.ret(context, service, "teacher"))
    m.append(diagram.self_message(
        service, "validateMeetings + teacherConflict + duplicateId"))

    alt_start = len(m)
    m.append(diagram.ret(service, page,
                         "InvalidInput / DuplicateIdentifier / "
                         "RecordNotFound / TimeConflict"))
    m.append(diagram.message(page, admin, "show reason, form stays open", kind="async"))

    else_start = len(m)
    m.append(diagram.message(service, context,
                             "applyTeachingClassChange(class)"))
    m.append(diagram.message(context, registration,
                             "applyCatalogChange(courses, classes)"))
    m.append(diagram.ret(service, page, "Success"))
    m.append(diagram.message(page, admin, "refresh catalog list", kind="async"))

    diagram.fragment("alt", "[invalid input, missing reference, "
                            "duplicate id or teacher conflict]",
                     alt_start, len(m) - 1,
                     dividers=[(else_start, "[all checks pass]")])

    diagram.note(service, "enroll() is not involved: no enrollment record "
                          "is created or changed here", m[7], width=210)

    save(diagram, "fig3_uc13_manage_courses_and_teaching_classes",
         "Fig3 UC13 Manage Courses and Teaching Classes")


# ---------------------------------------------------------------------------
# 图 4：UC14 Set Teaching Class Capacity
# ---------------------------------------------------------------------------

def figure4_uc14():
    diagram = SequenceDiagram(
        "Figure 4  UC14 Set Teaching Class Capacity",
        "The new capacity may not be lower than the number of active "
        "enrollments; remaining seats are derived, never stored")

    admin = diagram.participant("Administrator", kind="actor")
    page = diagram.participant("AdminPage")
    service = diagram.participant("AdministratorService")
    context = diagram.participant("SystemContext")
    registration = diagram.participant("RegistrationService")

    m = []
    m.append(diagram.message(admin, page,
                             "set the capacity of TC-10 to newCapacity"))
    m.append(diagram.message(page, service,
                             "setTeachingClassCapacity(identity, TC-10, "
                             "newCapacity)"))
    m.append(diagram.self_message(service, "canManageCatalog(identity)"))
    m.append(diagram.message(service, context, "findTeachingClass(TC-10)"))
    m.append(diagram.ret(context, service, "class, capacity = 2"))
    m.append(diagram.message(service, context, "enrollmentCount(TC-10)"))
    m.append(diagram.message(context, registration, "countActive(TC-10)"))
    m.append(diagram.ret(registration, context, "activeCount = 2"))
    m.append(diagram.ret(context, service, "activeCount"))

    alt_start = len(m)
    m.append(diagram.ret(service, page,
                         "CapacityBelowEnrollment(newCapacity, activeCount)"))
    m.append(diagram.message(page, admin,
                             "show reason, capacity unchanged", kind="async"))

    else_start = len(m)
    m.append(diagram.message(service, context,
                             "applyTeachingClassChange(class, newCapacity)"))
    m.append(diagram.message(context, registration,
                             "applyCatalogChange(courses, classes)"))
    m.append(diagram.ret(service, page,
                         "Success(remainingSeats = newCapacity - activeCount)"))
    m.append(diagram.message(page, admin,
                             "show new capacity and remaining seats",
                             kind="async"))

    diagram.fragment("alt", "[newCapacity < activeCount]",
                     alt_start, len(m) - 1,
                     dividers=[(else_start, "[newCapacity >= activeCount]")])

    diagram.note(page, "example with activeCount = 2: newCapacity = 1 is "
                       "rejected; newCapacity = 3 gives remainingSeats = 1",
                 m[4], width=250)

    save(diagram, "fig4_uc14_set_teaching_class_capacity",
         "Fig4 UC14 Set Teaching Class Capacity")


# ---------------------------------------------------------------------------
# 图 5：UC15 Set Registration Period
# ---------------------------------------------------------------------------

def figure5_uc15():
    diagram = SequenceDiagram(
        "Figure 5  UC15 Set Registration Period",
        "The administrator sets the opening and closing time; the open state "
        "is derived at request time, not cached")

    admin = diagram.participant("Administrator", kind="actor")
    page = diagram.participant("AdminPage")
    service = diagram.participant("AdministratorService")
    context = diagram.participant("SystemContext")
    registration = diagram.participant("RegistrationService")

    m = []
    m.append(diagram.message(admin, page,
                             "set period 2026-11-02T08:00 - "
                             "2026-11-13T18:00"))
    m.append(diagram.message(page, service,
                             "setRegistrationPeriod(identity, open, close)"))
    m.append(diagram.self_message(service, "canSetRegistrationPeriod(identity)"))
    m.append(diagram.self_message(service, "validate both timestamps"))

    alt_start = len(m)
    m.append(diagram.ret(service, page,
                         "InvalidInput (malformed or open >= close)"))
    m.append(diagram.message(page, admin,
                             "show reason, previous rule kept", kind="async"))

    else_start = len(m)
    m.append(diagram.message(service, context,
                             "applyRuleChange(opens_at, closes_at)"))
    m.append(diagram.message(context, registration, "applyRuleChange(rule)"))
    m.append(diagram.message(service, context, "registrationWindowOpen(now)"))
    m.append(diagram.self_message(
        context, "now >= opens_at and now < closes_at"))
    m.append(diagram.ret(service, page, "Success(open at now = true)"))
    m.append(diagram.message(page, admin,
                             "show period and current state", kind="async"))

    diagram.fragment("alt", "[malformed timestamp or start >= end]",
                     alt_start, len(m) - 1,
                     dividers=[(else_start, "[valid period]")])

    diagram.note(admin,
                 "the state is evaluated from the stored times on every "
                 "enroll / drop request, so it cannot go stale", m[8],
                 width=178)

    save(diagram, "fig5_uc15_set_registration_period",
         "Fig5 UC15 Set Registration Period")


# ---------------------------------------------------------------------------
# 图 6：UC16 Manage Student / Teacher Information
# ---------------------------------------------------------------------------

def figure6_uc16():
    diagram = SequenceDiagram(
        "Figure 6  UC16 Manage Student / Teacher Information",
        "Administrator creates a student account; duplicate identifiers and blank fields are rejected")

    admin = diagram.participant("Administrator", kind="actor")
    page = diagram.participant("AdminPage")
    service = diagram.participant("AdministratorService")
    context = diagram.participant("SystemContext")

    m = []
    m.append(diagram.message(admin, page,
                             'submit student "S2023004"'))
    m.append(diagram.message(page, service,
                             "createStudent(identity, record)"))
    m.append(diagram.self_message(service, "canManagePeople(identity)"))
    m.append(diagram.message(service, context, "findStudent(studentId)"))
    m.append(diagram.ret(context, service, "not found"))
    m.append(diagram.message(service, context,
                             "findStudentByNumber(studentNumber)"))
    m.append(diagram.ret(context, service, "not found"))
    m.append(diagram.self_message(
        service, "validate id > 0, number and name not blank"))

    alt_start = len(m)
    m.append(diagram.ret(service, page,
                         "DuplicateIdentifier / InvalidInput"))
    m.append(diagram.message(page, admin,
                             "show reason, no record written", kind="async"))

    else_start = len(m)
    m.append(diagram.message(service, context, "applyStudentChange(record)"))
    m.append(diagram.ret(service, page, "Success"))
    m.append(diagram.message(page, admin, "show new student and list", kind="async"))

    diagram.fragment("alt", "[duplicate id/number or blank field]",
                     alt_start, len(m) - 1,
                     dividers=[(else_start, "[record is valid]")])

    diagram.note(service,
                 "the same flow serves teachers: createTeacher / "
                 "updateTeacher / removeTeacher", m[2], width=230)

    save(diagram, "fig6_uc16_manage_student_teacher_information",
         "Fig6 UC16 Manage Student / Teacher Information")


def main():
    os.makedirs(OUT, exist_ok=True)
    figure1_object_diagram()
    figure2_uc09()
    figure3_uc13()
    figure4_uc14()
    figure5_uc15()
    figure6_uc16()
    print("done ->", OUT)


if __name__ == "__main__":
    main()
