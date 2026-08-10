module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
                %opt = gpm.optional_graph_pattern %1 (%arg1 : !tuples.tuplestream){
                    %2 = gpm.triple_pattern %arg1 @graphs::@ref(?{@vars::@who}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
                    %3 = gpm.triple_pattern %2 @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, id{"http://example.org/foo"})
                    tuples.return %3 : !tuples.tuplestream
                }
                tuples.return %opt : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@who, @vars::@age] => ["who", "age"] : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "age"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "age"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["who", "age"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "age"]>
        return
    }
}
