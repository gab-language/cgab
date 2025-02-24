local client = vim.lsp.start_client {
  name = "gablsp",
  cmd = { "gab", "run", "-s", "gab.lsp" },
  on_attach = function(server)
    vim.notify ("attatched")
  end,
}

if not client then
  vim.notify("Uh oh, failed to start lsp client")
  return
end

vim.api.nvim_create_autocmd("Filetype", {
  pattern = "gab",
  callback = function()
    vim.notify("Attaching client")
    vim.lsp.buf_attach_client(0, client)
  end,
})
