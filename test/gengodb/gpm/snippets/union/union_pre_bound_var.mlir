module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
                tuples.return %1 : !tuples.tuplestream
            }
            %left = gpm.basic_graph_pattern %bgp (%arg : !tuples.tuplestream){
                %2 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@who}, id{"http://example.org/drinks"}, ?{@l::@what({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %right = gpm.basic_graph_pattern %bgp (%arg : !tuples.tuplestream){
                %3 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@who}, id{"http://example.org/age"}, ?{@r::@what({type = !gpm.variable_binding})})
                tuples.return %3 : !tuples.tuplestream
            }
            %union = gpm.bag %left, %right mapping: {@union::@what({type = !gpm.variable_binding})=[@l::@what,@r::@what]}
            %res_table = relalg.materialize %union [@vars::@who, @union::@what] => ["who", "what"] : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        return
    }
}
