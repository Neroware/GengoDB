module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
                %2 = gpm.triple_pattern %1 @graphs::@ref(?{@vars::@person}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !gpm.variable_binding})})
                %3 = gpm.triple_pattern %2 @graphs::@ref(?{@vars::@cup}, id{"http://example.org/size"}, ?{@vars::@size({type = !gpm.variable_binding})})
                tuples.return %3 : !tuples.tuplestream
            }
            %filtered = relalg.selection %bgp (%arg0 : !tuples.tuple){
                %ageVal = tuples.getcol %arg0 @vars::@age : !gpm.variable_binding
                %sizeVal = tuples.getcol %arg0 @vars::@size : !gpm.variable_binding
                %ageVar = gpm.get_binding %ageVal -> !variant.variant
                %sizeVar = gpm.get_binding %sizeVal -> !variant.variant
                %prod = variant.arith mul %ageVar, %sizeVar
                %expect = arith.constant 30 : i32
                %expectVar = variant.create_scalar %expect : i32
                %pred = variant.cmp eq %prod, %expectVar -> !db.nullable<i1>
                tuples.return %pred : !db.nullable<i1>
            }
            %res_table = relalg.materialize %filtered [@vars::@person, @vars::@age, @vars::@size] => ["person", "age", "size"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "size"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "size"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "size"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "size"]>
        return
    }
}
