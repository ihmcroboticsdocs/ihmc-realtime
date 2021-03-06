/*
 *   Copyright 2014 Florida Institute for Human and Machine Cognition (IHMC)
 *    
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *    
 *    http://www.apache.org/licenses/LICENSE-2.0
 *    
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *    
 *    Written by Jesper Smith with assistance from IHMC team members
 */
package us.ihmc.realtime;


public class PriorityParameters
{
   
   private static final int maximumPriority = RealtimeNative.getMaximumPriorityNative();
   private static final int minimumPriority = RealtimeNative.getMinimumPriorityNative();
   
   public static final PriorityParameters MAXIMUM_PRIORITY = new PriorityParameters(maximumPriority);
   public static final PriorityParameters MINIMUM_PRIORITY = new PriorityParameters(minimumPriority);

   private final int priority;
   
   public PriorityParameters(int priority)
   {
      if(minimumPriority > priority || priority > maximumPriority)
      {
         throw new RuntimeException("Unsupported priority, requested " + priority + ", minimum: " + minimumPriority + "; maximum: " + maximumPriority);
      }
      
      this.priority = priority;
   }
   
   public int getPriority()
   {
      return this.priority;
   }
   
   public static int getMaximumPriority()
   {
      return maximumPriority;
   }
   
   public static int getMinimumPriority()
   {
      return minimumPriority;
   }
   
   /**
    * 
    * Get a relative priority. Useful for cross platform code.
    * 
    * @param relativePriority priority between 0 and 100, where 100 is the maximum priority
    * @return PriorityParameters
    */
   public static PriorityParameters getRelativePriority(int relativePriority)
   {
      if(relativePriority < 0 || relativePriority > 100)
      {
         throw new RuntimeException("The relative priority needs to be in the range from 0 to 100");
      }
      int priorityRange = maximumPriority - minimumPriority;
      int absolutePriority = minimumPriority + ((priorityRange * relativePriority) / 100);
      return new PriorityParameters(absolutePriority);
   }
}
