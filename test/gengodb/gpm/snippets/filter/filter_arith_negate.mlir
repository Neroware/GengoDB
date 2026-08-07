module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %filtered = relalg.selection %bgp (%arg0 : !tuples.tuple){
                %ageVal = tuples.getcol %arg0 @vars::@age : !gpm.variable_binding
                %ageVar = gpm.get_binding %ageVal -> !variant.variant
                %zero = arith.constant 0 : i32
                %zeroVar = variant.create_scalar %zero : i32
                %negVar = variant.arith sub %zeroVar, %ageVar
                %pred = variant.cmp lt %negVar, %zeroVar -> !db.nullable<i1>
                tuples.return %pred : !db.nullable<i1>
            }
            %res_table = relalg.materialize %filtered [@vars::@person, @vars::@age] => ["person", "age"] : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        return
    }
}
