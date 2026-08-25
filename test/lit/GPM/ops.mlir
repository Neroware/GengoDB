// RUN: mlir-db-opt -allow-unregistered-dialect %s -split-input-file -mlir-print-debuginfo -mlir-print-local-scope  | FileCheck %s
module {
    func.func @main() {
        //CHECK: %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        //CHECK: %1 = gpm.basic_graph_pattern %0 (%arg0: !tuples.tuplestream){
        %1 = gpm.basic_graph_pattern %0 (%arg0 : !tuples.tuplestream){
            //CHECK: %2 = gpm.triple_pattern %arg0 @graphs::@ref(id{"http://example.org/bob"}, ?{@vars::@what({type = !gpm.variable_binding})}, id{"http://example.org/coffee"})
            %2 = gpm.triple_pattern %arg0 @graphs::@ref(id{"http://example.org/bob"}, ?{@vars::@what({type = !gpm.variable_binding})}, id{"http://example.org/coffee"})
            //CHECK: %3 = gpm.filter %2 (%arg1: !tuples.tuple){
            %3 = gpm.filter %2 (%arg1: !tuples.tuple) {
                //CHECK: %4 = db.constant(1 : i64) : i1
                %4 = db.constant ( 1 ) : i1
                //CHECK: tuples.return %4 : i1
                tuples.return %4 : i1
            }
            tuples.return %3 : !tuples.tuplestream
        }
        return
    }
}
