module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %opt = gpm.optional_graph_pattern %bgp (%arg1 : !tuples.tuplestream){
                %2 = gpm.triple_pattern %arg1 @graphs::@ref(?{@vars::@drink}, id{"http://example.org/temp"}, ?{@vars::@t({type = !gpm.variable_binding})})
                %nested = gpm.optional_graph_pattern %2 (%arg2 : !tuples.tuplestream){
                    %3 = gpm.triple_pattern %arg2 @graphs::@ref(?{@vars::@drink}, id{"http://example.org/strong"}, ?{@vars::@s({type = !gpm.variable_binding})})
                    tuples.return %3 : !tuples.tuplestream
                }
                tuples.return %nested : !tuples.tuplestream
            }
            %res_table = relalg.materialize %opt [@vars::@drink, @vars::@t, @vars::@s] => ["drink", "t", "s"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["drink", "t", "s"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["drink", "t", "s"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["drink", "t", "s"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["drink", "t", "s"]>
        return
    }
}
