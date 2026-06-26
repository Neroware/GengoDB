module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph name : "file://resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, id{"http://example.org/coffee"})
                %2 = gpm.triple_pattern %1 @graphs::@coffee(?{@vars::@who2({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
                tuples.return %2 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@who, @vars::@who2] => ["coffee", "sushi"] : !subop.local_table<[col1: !db.string, col2: !db.string],["coffee", "sushi"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["coffee", "sushi"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["coffee", "sushi"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["coffee", "sushi"]>
        return
    }
}