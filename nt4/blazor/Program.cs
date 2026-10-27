using Microsoft.AspNetCore.Http.HttpResults;
using Nt4Blazor;
using Nt4Blazor.Components;
using Nt4Blazor.Components.Lite;

var builder = WebApplication.CreateBuilder(args);

// Add services to the container.
builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();
builder.Services.AddSingleton<LiteTimer>();

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

// Page for the NT 4 litehtml client. Commands redirect back so a refresh does not repeat them.
app.MapGet("/lite", IResult (string? cmd, LiteTimer timer) =>
{
    switch (cmd)
    {
        case "start": timer.Start(); break;
        case "stop": timer.Stop(); break;
        case "reset": timer.Reset(); break;
        default: return new RazorComponentResult<LitePage>();
    }
    return Results.Redirect("/lite");
});

app.MapStaticAssets();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

app.Run();
