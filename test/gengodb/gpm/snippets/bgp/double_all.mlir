module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@s({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
                %2 = gpm.triple_pattern %1 @graphs::@ref(?{@vars::@s}, ?{@vars::@p}, ?{@vars::@o})
                tuples.return %2 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@s, @vars::@p, @vars::@o] => ["s", "p", "o"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["s", "p", "o"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["s", "p", "o"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["s", "p", "o"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["s", "p", "o"]>
        return
    }
}
