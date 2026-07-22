module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
                %sub = gpm.basic_graph_pattern %1 (%arg1 : !tuples.tuplestream){
                    %2 = gpm.triple_pattern %arg1 @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !gpm.variable_binding})})
                    tuples.return %2 : !tuples.tuplestream
                }
                %3 = gpm.triple_pattern %sub @graphs::@ref(?{@vars::@drink}, id{"http://example.org/temp"}, ?{@vars::@temp({type = !gpm.variable_binding})})
                tuples.return %3 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@what, @vars::@drink, @vars::@temp] => ["what", "drink", "temp"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["what", "drink", "temp"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["what", "drink", "temp"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["what", "drink", "temp"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["what", "drink", "temp"]>
        return
    }
}
