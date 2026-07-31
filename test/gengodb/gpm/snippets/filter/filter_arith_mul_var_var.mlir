module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
                %2 = gpm.triple_pattern %1 @graphs::@ref(?{@vars::@person}, id{"http://example.org/tableNumber"}, ?{@vars::@table({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %filtered = relalg.selection %bgp (%arg0 : !tuples.tuple){
                %ageVal = tuples.getcol %arg0 @vars::@age : !gpm.variable_binding
                %tableVal = tuples.getcol %arg0 @vars::@table : !gpm.variable_binding
                %ageLex, %ageType = xsd.literal_of_ref %ageVal : !gpm.variable_binding -> !db.nullable<!util.varlen32>, !db.nullable<i32>
                %tableLex, %tableType = xsd.literal_of_ref %tableVal : !gpm.variable_binding -> !db.nullable<!util.varlen32>, !db.nullable<i32>
                %prodLex, %prodType = xsd.arith mul %ageLex : !db.nullable<!util.varlen32>, %ageType : !db.nullable<i32>, %tableLex : !db.nullable<!util.varlen32>, %tableType : !db.nullable<i32> -> !db.nullable<!util.varlen32>, !db.nullable<i32>
                %expectLexRaw = util.varlen32_create_const "360"
                %expectLex = db.as_nullable %expectLexRaw : !util.varlen32 -> !db.nullable<!util.varlen32>
                %expectTypeRaw = db.constant ( 200 ) : i32
                %expectType = db.as_nullable %expectTypeRaw : i32 -> !db.nullable<i32>
                %pred = xsd.compare_dyn eq %prodLex : !db.nullable<!util.varlen32>, %prodType : !db.nullable<i32>, %expectLex : !db.nullable<!util.varlen32>, %expectType : !db.nullable<i32> -> !db.nullable<i1>
                tuples.return %pred : !db.nullable<i1>
            }
            %res_table = relalg.materialize %filtered [@vars::@person, @vars::@age, @vars::@table] => ["person", "age", "table"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "table"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "table"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "table"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["person", "age", "table"]>
        return
    }
}
