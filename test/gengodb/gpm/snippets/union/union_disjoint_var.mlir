module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %left = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@l::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@l::@what({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %right = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %2 = gpm.triple_pattern %arg @graphs::@ref(?{@r::@who({type = !gpm.variable_binding})}, id{"http://example.org/age"}, ?{@r::@age({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %union = gpm.bag %left, %right mapping: {@union::@who({type = !gpm.variable_binding})=[@l::@who,@r::@who], @union::@what({type = !db.nullable<!gpm.variable_binding>})=[@l::@what,unit], @union::@age({type = !db.nullable<!gpm.variable_binding>})=[unit,@r::@age]}
            %res_table = relalg.materialize %union [@union::@who, @union::@what, @union::@age] => ["who", "what", "age"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "age"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "age"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "age"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "age"]>
        return
    }
}
