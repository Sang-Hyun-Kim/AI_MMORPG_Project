using Microsoft.EntityFrameworkCore;
using WebBackend.Models;

namespace WebBackend.Data
{
    public class ApplicationDbContext : DbContext
    {
        public ApplicationDbContext(DbContextOptions<ApplicationDbContext> options)
            : base(options)
        {
        }

        public DbSet<Account> Accounts { get; set; }
        public DbSet<Player> Players { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            modelBuilder.Entity<Account>()
                .HasIndex(a => a.AccountName)
                .IsUnique();

            modelBuilder.Entity<Player>()
                .HasIndex(p => p.PlayerName)
                .IsUnique();
        }
    }
}
