print("HELLO FROM SCRIPT");
print_info("HELLO FROM SCRIPT");
print_warn("HELLO FROM SCRIPT");
print_err("HELLO FROM SCRIPT");
print_crit("HELLO FROM SCRIPT");

function init() {
  print("init");
}

function tick() {
  print("tick");
}

function update() {
  print(Time.deltaTime);
}
