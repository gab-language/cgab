def Fib(i)
  return i < 2 && i || Fib(i - 1) + Fib(i - 2)
end

Fib(35)
