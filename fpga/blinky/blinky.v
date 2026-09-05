`default_nettype none   //do not allow undeclared wires

module blinky (
    input  wire clk,
    output wire [2:0] led
    );

    reg [24:0] r_count = 0;

    always @(posedge(clk)) r_count <= r_count + 1;

    assign led = {3{r_count[24]}};
endmodule
