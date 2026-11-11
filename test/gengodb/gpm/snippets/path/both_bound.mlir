// ?x ex:rel ?y . ?y ex:rel+ ?x  (both ends bound: reachability filter) -> all 5 rel triples
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@x({type = !gpm.variable_binding})}, id{"http://example.org/rel"}, ?{@vars::@y({type = !gpm.variable_binding})})
            %out = gpm.relalg.path_expansion %1 @graphs::@ref(?{@vars::@y} -> ?{@vars::@x}) step: [id{"http://example.org/rel"}] min: 1 anchor: outgoing
            %res_table = relalg.materialize %out [@vars::@x, @vars::@y] => ["x", "y"] : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        return
    }
}
