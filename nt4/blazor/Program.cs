using Microsoft.AspNetCore.Http.HttpResults;
using Nt4Blazor;
using Nt4Blazor.Components;
using Nt4Blazor.Components.Lite;

var builder = WebApplication.CreateBuilder(args);

// Add services to the container.
builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();
builder.Services.AddSingleton<LiteTimer>();
builder.Services.AddSingleton<LiteChartData>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<LiteChartData>());

var app = builder.Build();

// Configure the HTTP request pipeline.
if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error", createScopeForErrors: true);
    // The default HSTS value is 30 days. You may want to change this for production scenarios, see https://aka.ms/aspnetcore-hsts.
    app.UseHsts();
}
app.UseStatusCodePagesWithReExecute("/not-found", createScopeForStatusCodePages: true);
// No HTTPS redirect: Opera 10.63 on NT 4 cannot do current TLS.

app.UseAntiforgery();

app.MapGet("/api/zeit", () => DateTime.Now.ToString("HH:mm:ss"));

// NT 4 pages, SVG and text API under /nt4 (and /lite as alias for older NT4LITE.EXE builds).
Nt4Routes.Map(app);

app.MapStaticAssets();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

app.Run();
