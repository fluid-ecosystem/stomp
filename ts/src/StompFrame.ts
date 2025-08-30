// STOMP Frame representation
export interface StompFrame {
  command: string;
  headers: Record<string, string>;
  body: string;
}

export class StompFrameUtils {
  static parse(frameData: string): StompFrame | null {
    if (!frameData || frameData.trim().length === 0) return null;
    
    // Use split with limit to preserve trailing empty strings
    const lines = frameData.split('\n');
    if (lines.length === 0) return null;
    
    const command = lines[0]!.trim();
    const headers: Record<string, string> = {};
    let bodyStart = 1;
    
    // Parse headers
    for (let i = 1; i < lines.length; i++) {
      const line = lines[i]!;
      if (line.length === 0) {
        bodyStart = i + 1;
        break;
      }
      const colonIndex = line.indexOf(':');
      if (colonIndex !== -1) {
        const key = line.substring(0, colonIndex).trim();
        const value = line.substring(colonIndex + 1).trim();
        headers[key] = value;
      }
    }
    
    // Parse body
    const bodyLines = lines.slice(bodyStart);
    let body = bodyLines.join('\n');
    
    // Remove null terminator if present
    if (body.endsWith('\0')) {
      body = body.substring(0, body.length - 1);
    }
    
    return { command, headers, body };
  }
  
  static serialize(frame: StompFrame): string {
    let result = frame.command + '\n';
    
    // Add headers
    for (const [key, value] of Object.entries(frame.headers)) {
      result += `${key}:${value}\n`;
    }
    
    // Empty line separator
    result += '\n';
    
    // Add body
    if (frame.body && frame.body.length > 0) {
      result += frame.body;
    }
    
    // Null terminator
    result += '\0';
    
    return result;
  }
  
  // Client-side frame builders
  static connect(host: string, login?: string, passcode?: string): StompFrame {
    const headers: Record<string, string> = {
      'accept-version': '1.2',
      'host': host
    };
    
    if (login) headers['login'] = login;
    if (passcode) headers['passcode'] = passcode;
    
    return { command: 'CONNECT', headers, body: '' };
  }
  
  static subscribe(destination: string, subscriptionId: string): StompFrame {
    return {
      command: 'SUBSCRIBE',
      headers: { destination, id: subscriptionId },
      body: ''
    };
  }
  
  static unsubscribe(subscriptionId: string): StompFrame {
    return {
      command: 'UNSUBSCRIBE',
      headers: { id: subscriptionId },
      body: ''
    };
  }
  
  static send(destination: string, body: string): StompFrame {
    return {
      command: 'SEND',
      headers: { destination },
      body
    };
  }
  
  static disconnect(): StompFrame {
    return { command: 'DISCONNECT', headers: {}, body: '' };
  }
  
  // Server-side frame builders
  static connected(session: string): StompFrame {
    return {
      command: 'CONNECTED',
      headers: { version: '1.2', session },
      body: ''
    };
  }
  
  static receipt(receiptId: string): StompFrame {
    return {
      command: 'RECEIPT',
      headers: { 'receipt-id': receiptId },
      body: ''
    };
  }
  
  static message(destination: string, messageId: string, subscription: string, body: string): StompFrame {
    return {
      command: 'MESSAGE',
      headers: { destination, 'message-id': messageId, subscription },
      body
    };
  }
  
  static error(message: string, details: string): StompFrame {
    return {
      command: 'ERROR',
      headers: { message },
      body: details
    };
  }
}
