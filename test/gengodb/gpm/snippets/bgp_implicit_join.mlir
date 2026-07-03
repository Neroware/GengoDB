module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph name : "file://resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
            %bgp0 = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@coffee(id{"http://example.org/bob"}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %bgp1 = gpm.basic_graph_pattern %bgp0 (%arg1 : !tuples.tuplestream){
                %2 = gpm.triple_pattern %arg1 @graphs::@coffee(id{"http://example.org/bob"}, id{"http://example.org/drinks"}, ?{@vars::@what})
                tuples.return %2 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp1 [@vars::@what] => ["what"] : !subop.local_table<[col1: !db.string],["what"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["what"]>
        } -> !subop.local_table<[col1: !db.string],["what"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["what"]>
        return
    }
}
