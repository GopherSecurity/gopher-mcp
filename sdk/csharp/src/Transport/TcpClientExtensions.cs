using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace GopherMcp.Transport
{
    internal static class TcpClientExtensions
    {
#if NETSTANDARD2_1
        public static async Task ConnectAsync(this TcpClient client, string host, int port, CancellationToken cancellationToken)
        {
            using (cancellationToken.Register(() => client.Close()))
            {
                try
                {
                    await client.ConnectAsync(host, port).ConfigureAwait(false);
                }
                catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
                {
                    throw new OperationCanceledException(cancellationToken);
                }
            }
        }
#endif
    }
}
