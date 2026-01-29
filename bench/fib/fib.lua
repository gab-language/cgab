function Fib(n)
  return n < 2 and n or Fib(n - 1) + Fib(n - 2)
end

Fib(35)
