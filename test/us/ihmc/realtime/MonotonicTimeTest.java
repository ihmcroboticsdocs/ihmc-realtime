package us.ihmc.realtime;

import static org.junit.Assert.assertEquals;

import java.util.Random;

import org.junit.Test;

public class MonotonicTimeTest
{
   public static final long NANOSECONDS_PER_SECOND = 1000000000;

   private void normalize(long seconds, long nanoseconds)
   {
      while (nanoseconds >= NANOSECONDS_PER_SECOND)
      {
         nanoseconds -= NANOSECONDS_PER_SECOND;
         seconds++;
      }

      while (nanoseconds < 0)
      {
         nanoseconds += NANOSECONDS_PER_SECOND;
         seconds--;
      }
   }

   @Test
   public void testNormalize()
   {
      Random random = new Random(151857L);
      
      MonotonicTime time = new MonotonicTime();
      for(int i = 0; i < 10; i++)
      {
         long seconds = random.nextInt(1 << 30);
         long nanoseconds = Math.abs(random.nextLong());
         
         time.set(seconds, nanoseconds);
         
         while (nanoseconds >= NANOSECONDS_PER_SECOND)
         {
            nanoseconds -= NANOSECONDS_PER_SECOND;
            seconds++;
         }

         while (nanoseconds < 0)
         {
            nanoseconds += NANOSECONDS_PER_SECOND;
            seconds--;
         }
         
         assertEquals(seconds, time.seconds());
         assertEquals(nanoseconds, time.nanoseconds());
      }
   }

}
