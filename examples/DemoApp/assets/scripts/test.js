print("HELLO FROM SCRIPT");
print_info("HELLO FROM SCRIPT");
print_warn("HELLO FROM SCRIPT");
print_err("HELLO FROM SCRIPT");
print_crit("HELLO FROM SCRIPT");

function add(a, b) {
  return a + b;
}

function testArgs(int, str, bool, float) {
  print(int);
  print(str);
  print(bool);
  print(float);
}

print(age);
var age = 69;
var name = "John";
print(hello);

function init() {
  print("init");
}

function tick() {
  print("tick");
}

function update() {
  print(Time.deltaTime);
  print_err(age);
}
