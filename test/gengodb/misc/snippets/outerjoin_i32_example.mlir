// RUN: run-mlir %s
//
// Example: two i32-based relations combined with a left outer join.
// `employees` has an `id`/`dept_id` pair; `departments` has `dept_id`/`budget`.
// dept_id == 4 in `employees` has no match in `departments`, so the outer
// join produces a NULL for `dept_budget` in that row.

module {
  func.func @main() {
    %result = relalg.query () {
      // relation 1: employees(id, dept_id)
      %employees = relalg.const_relation columns : [@employees::@id({type = i32}), @employees::@dept_id({type = i32})]
        values : [[1, 10], [2, 20], [3, 20], [4, 40]]

      // relation 2: departments(dept_id, budget) -- note: no row for dept_id == 40
      %departments = relalg.const_relation columns : [@departments::@dept_id({type = i32}), @departments::@budget({type = i32})]
        values : [[10, 100000], [20, 250000], [30, 90000]]

      // left outer join employees against departments on dept_id
      %joined = relalg.outerjoin %employees, %departments (%arg0: !tuples.tuple) {
        %lhs = tuples.getcol %arg0 @employees::@dept_id : i32
        %rhs = tuples.getcol %arg0 @departments::@dept_id : i32
        %eq = db.compare eq %lhs : i32, %rhs : i32
        tuples.return %eq : i1
      } mapping: {@outerjoin::@budget({type = !db.nullable<i32>}) = [@departments::@budget]}

      %materialized = relalg.materialize %joined [@employees::@id, @employees::@dept_id, @outerjoin::@budget]
        => ["emp_id", "dept_id", "dept_budget"]
        : !subop.local_table<[emp_id : i32, dept_id : i32, dept_budget : !db.nullable<i32>], ["emp_id", "dept_id", "dept_budget"]>

      relalg.query_return %materialized : !subop.local_table<[emp_id : i32, dept_id : i32, dept_budget : !db.nullable<i32>], ["emp_id", "dept_id", "dept_budget"]>
    } -> !subop.local_table<[emp_id : i32, dept_id : i32, dept_budget : !db.nullable<i32>], ["emp_id", "dept_id", "dept_budget"]>

    subop.set_result 0 %result : !subop.local_table<[emp_id : i32, dept_id : i32, dept_budget : !db.nullable<i32>], ["emp_id", "dept_id", "dept_budget"]>
    return
  }
}
