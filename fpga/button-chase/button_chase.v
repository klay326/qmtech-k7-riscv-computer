`default_nettype none

module button_chase (
    input  wire clk,        // 50MHz, F22
    input  wire sw2_n,      // pause/resume, active low, V26
    input  wire sw3_n,      // reverse direction, active low, U26
    output wire [2:0] led   // active low, R26/P26/N26
);

    // ~3 steps/sec chase rate at 50MHz
    reg [23:0] clkdiv = 0;
    always @(posedge clk) clkdiv <= clkdiv + 1;
    wire tick = (clkdiv == 0);

    // 2-flop synchronizers for the async buttons
    reg [1:0] sw2_sync = 2'b11;
    reg [1:0] sw3_sync = 2'b11;
    always @(posedge clk) begin
        sw2_sync <= {sw2_sync[0], sw2_n};
        sw3_sync <= {sw3_sync[0], sw3_n};
    end
    wire paused  = ~sw2_sync[1];
    wire reverse = ~sw3_sync[1];

    reg [1:0] pos = 0;
    always @(posedge clk) begin
        if (tick && !paused) begin
            if (reverse)
                pos <= (pos == 0) ? 2'd2 : pos - 2'd1;
            else
                pos <= (pos == 2) ? 2'd0 : pos + 2'd1;
        end
    end

    assign led[0] = (pos == 0) ? 1'b0 : 1'b1;
    assign led[1] = (pos == 1) ? 1'b0 : 1'b1;
    assign led[2] = (pos == 2) ? 1'b0 : 1'b1;

endmodule
