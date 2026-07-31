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
                %ageLex, %ageType = xsd.literal_of_ref %ageVal : !gpm.variable_binding -> !db.nullable<!util.varlen32>, !db.nullable<i32>
                %oneLexRaw = util.varlen32_create_const "1"
                %oneLex = db.as_nullable %oneLexRaw : !util.varlen32 -> !db.nullable<!util.varlen32>
                %oneTypeRaw = db.constant ( 200 ) : i32
                %oneType = db.as_nullable %oneTypeRaw : i32 -> !db.nullable<i32>
                %sumLex, %sumType = xsd.arith add %ageLex : !db.nullable<!util.varlen32>, %ageType : !db.nullable<i32>, %oneLex : !db.nullable<!util.varlen32>, %oneType : !db.nullable<i32> -> !db.nullable<!util.varlen32>, !db.nullable<i32>
                %thirtyLexRaw = util.varlen32_create_const "30"
                %thirtyLex = db.as_nullable %thirtyLexRaw : !util.varlen32 -> !db.nullable<!util.varlen32>
                %thirtyTypeRaw = db.constant ( 200 ) : i32
                %thirtyType = db.as_nullable %thirtyTypeRaw : i32 -> !db.nullable<i32>
                %pred = xsd.compare_dyn gt %sumLex : !db.nullable<!util.varlen32>, %sumType : !db.nullable<i32>, %thirtyLex : !db.nullable<!util.varlen32>, %thirtyType : !db.nullable<i32> -> !db.nullable<i1>
                tuples.return %pred : !db.nullable<i1>
            }
            %res_table = relalg.materialize %filtered [@vars::@person, @vars::@age] => ["person", "age"] : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["person", "age"]>
        return
    }
}
