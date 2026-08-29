using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace WebBackend.Models
{
    [Table("Player")]
    public class Player
    {
        [Key]
        public int PlayerId { get; set; }

        [Required]
        [StringLength(50)]
        public string PlayerName { get; set; } = string.Empty;

        public int AccountId { get; set; }
        
        [ForeignKey("AccountId")]
        public Account Account { get; set; } = null!;
    }
}
